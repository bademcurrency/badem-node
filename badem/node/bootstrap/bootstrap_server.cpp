#include <badem/node/bootstrap/bootstrap_server.hpp>
#include <badem/node/node.hpp>
#include <badem/node/transport/tcp.hpp>

badem::bootstrap_listener::bootstrap_listener (uint16_t port_a, badem::node & node_a) :
node (node_a),
port (port_a)
{
}

void badem::bootstrap_listener::start ()
{
	listening_socket = std::make_shared<badem::server_socket> (node.shared (), boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port), node.config.tcp_incoming_connections_max);
	boost::system::error_code ec;
	listening_socket->start (ec);
	if (ec)
	{
		node.logger.try_log (boost::str (boost::format ("Error while binding for incoming TCP/bootstrap on port %1%: %2%") % listening_socket->listening_port () % ec.message ()));
		throw std::runtime_error (ec.message ());
	}
	listening_socket->on_connection ([this](std::shared_ptr<badem::socket> new_connection, boost::system::error_code const & ec_a) {
		bool keep_accepting = true;
		if (ec_a)
		{
			keep_accepting = false;
			this->node.logger.try_log (boost::str (boost::format ("Error while accepting incoming TCP/bootstrap connections: %1%") % ec_a.message ()));
		}
		else
		{
			accept_action (ec_a, new_connection);
		}
		return keep_accepting;
	});
}

void badem::bootstrap_listener::stop ()
{
	decltype (connections) connections_l;
	{
		badem::lock_guard<std::mutex> lock (mutex);
		on = false;
		connections_l.swap (connections);
	}
	if (listening_socket)
	{
		listening_socket->close ();
		listening_socket = nullptr;
	}
}

size_t badem::bootstrap_listener::connection_count ()
{
	badem::lock_guard<std::mutex> lock (mutex);
	return connections.size ();
}

void badem::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<badem::socket> socket_a)
{
	auto connection (std::make_shared<badem::bootstrap_server> (socket_a, node.shared ()));
	{
		badem::lock_guard<std::mutex> lock (mutex);
		connections[connection.get ()] = connection;
		connection->receive ();
	}
}

boost::asio::ip::tcp::endpoint badem::bootstrap_listener::endpoint ()
{
	return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), listening_socket->listening_port ());
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_listener & bootstrap_listener, const std::string & name)
{
	auto sizeof_element = sizeof (decltype (bootstrap_listener.connections)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "connections", bootstrap_listener.connection_count (), sizeof_element }));
	return composite;
}
}

badem::bootstrap_server::bootstrap_server (std::shared_ptr<badem::socket> socket_a, std::shared_ptr<badem::node> node_a) :
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
socket (socket_a),
node (node_a)
{
	receive_buffer->resize (1024);
}

badem::bootstrap_server::~bootstrap_server ()
{
	if (node->config.logging.bulk_pull_logging ())
	{
		node->logger.try_log ("Exiting incoming TCP/bootstrap server");
	}
	if (type == badem::bootstrap_server_type::bootstrap)
	{
		--node->bootstrap.bootstrap_count;
	}
	else if (type == badem::bootstrap_server_type::realtime)
	{
		--node->bootstrap.realtime_count;
		// Clear temporary channel
		auto exisiting_response_channel (node->network.tcp_channels.find_channel (remote_endpoint));
		if (exisiting_response_channel != nullptr)
		{
			exisiting_response_channel->server = false;
			node->network.tcp_channels.erase (remote_endpoint);
		}
	}
	stop ();
	badem::lock_guard<std::mutex> lock (node->bootstrap.mutex);
	node->bootstrap.connections.erase (this);
}

void badem::bootstrap_server::stop ()
{
	if (!stopped.exchange (true))
	{
		if (socket != nullptr)
		{
			socket->close ();
		}
	}
}

void badem::bootstrap_server::receive ()
{
	// Increase timeout to receive TCP header (idle server socket)
	socket->set_timeout (node->network_params.node.idle_timeout);
	auto this_l (shared_from_this ());
	socket->async_read (receive_buffer, 8, [this_l](boost::system::error_code const & ec, size_t size_a) {
		// Set remote_endpoint
		if (this_l->remote_endpoint.port () == 0)
		{
			this_l->remote_endpoint = this_l->socket->remote_endpoint ();
		}
		// Decrease timeout to default
		this_l->socket->set_timeout (this_l->node->config.tcp_io_timeout);
		// Receive header
		this_l->receive_header_action (ec, size_a);
	});
}

void badem::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		assert (size_a == 8);
		badem::bufferstream type_stream (receive_buffer->data (), size_a);
		auto error (false);
		badem::message_header header (error, type_stream);
		if (!error)
		{
			auto this_l (shared_from_this ());
			switch (header.type)
			{
				case badem::message_type::bulk_pull:
				{
					node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::bulk_pull, badem::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::bulk_pull_account:
				{
					node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::bulk_pull_account, badem::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_account_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::frontier_req:
				{
					node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::frontier_req, badem::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_frontier_req_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::bulk_push:
				{
					node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::bulk_push, badem::stat::dir::in);
					if (is_bootstrap_connection ())
					{
						add_request (std::unique_ptr<badem::message> (new badem::bulk_push (header)));
					}
					break;
				}
				case badem::message_type::keepalive:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_keepalive_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::publish:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_publish_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::confirm_ack:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_confirm_ack_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::confirm_req:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_confirm_req_action (ec, size_a, header);
					});
					break;
				}
				case badem::message_type::node_id_handshake:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_node_id_handshake_action (ec, size_a, header);
					});
					break;
				}
				default:
				{
					if (node->config.logging.network_logging ())
					{
						node->logger.try_log (boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (header.type)));
					}
					break;
				}
			}
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error while receiving type: %1%") % ec.message ()));
		}
	}
}

void badem::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::bulk_pull> request (new badem::bulk_pull (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received bulk pull for %1% down to %2%, maximum of %3%") % request->start.to_string () % request->end.to_string () % (request->count ? request->count : std::numeric_limits<double>::infinity ())));
			}
			if (is_bootstrap_connection () && !node->flags.disable_bootstrap_bulk_pull_server)
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
}

void badem::bootstrap_server::receive_bulk_pull_account_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		assert (size_a == header_a.payload_length_bytes ());
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::bulk_pull_account> request (new badem::bulk_pull_account (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received bulk pull account for %1% with a minimum amount of %2%") % request->account.to_account () % badem::amount (request->minimum_amount).format_balance (badem::Mbdm_ratio, 10, true)));
			}
			if (is_bootstrap_connection () && !node->flags.disable_bootstrap_bulk_pull_server)
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
}

void badem::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::frontier_req> request (new badem::frontier_req (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age));
			}
			if (is_bootstrap_connection ())
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error sending receiving frontier request: %1%") % ec.message ()));
		}
	}
}

void badem::bootstrap_server::receive_keepalive_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::keepalive> request (new badem::keepalive (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_keepalive_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error receiving keepalive: %1%") % ec.message ()));
		}
	}
}

void badem::bootstrap_server::receive_publish_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::publish> request (new badem::publish (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_message_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error receiving publish: %1%") % ec.message ()));
		}
	}
}

void badem::bootstrap_server::receive_confirm_req_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::confirm_req> request (new badem::confirm_req (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_message_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving confirm_req: %1%") % ec.message ()));
	}
}

void badem::bootstrap_server::receive_confirm_ack_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::confirm_ack> request (new badem::confirm_ack (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_message_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving confirm_ack: %1%") % ec.message ()));
	}
}

void badem::bootstrap_server::receive_node_id_handshake_action (boost::system::error_code const & ec, size_t size_a, badem::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		badem::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<badem::node_id_handshake> request (new badem::node_id_handshake (error, stream, header_a));
		if (!error)
		{
			if (type == badem::bootstrap_server_type::undefined && !node->flags.disable_tcp_realtime)
			{
				add_request (std::unique_ptr<badem::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_node_id_handshake_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving node_id_handshake: %1%") % ec.message ()));
	}
}

void badem::bootstrap_server::add_request (std::unique_ptr<badem::message> message_a)
{
	assert (message_a != nullptr);
	badem::lock_guard<std::mutex> lock (mutex);
	auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void badem::bootstrap_server::finish_request ()
{
	badem::lock_guard<std::mutex> lock (mutex);
	requests.pop ();
	if (!requests.empty ())
	{
		run_next ();
	}
	else
	{
		std::weak_ptr<badem::bootstrap_server> this_w (shared_from_this ());
		node->alarm.add (std::chrono::steady_clock::now () + (node->config.tcp_io_timeout * 2) + std::chrono::seconds (1), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->timeout ();
			}
		});
	}
}

void badem::bootstrap_server::finish_request_async ()
{
	std::weak_ptr<badem::bootstrap_server> this_w (shared_from_this ());
	node->background ([this_w]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->finish_request ();
		}
	});
}

void badem::bootstrap_server::timeout ()
{
	if (socket != nullptr)
	{
		if (socket->has_timed_out ())
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log ("Closing incoming tcp / bootstrap server by timeout");
			}
			{
				badem::lock_guard<std::mutex> lock (node->bootstrap.mutex);
				node->bootstrap.connections.erase (this);
			}
			socket->close ();
		}
	}
	else
	{
		badem::lock_guard<std::mutex> lock (node->bootstrap.mutex);
		node->bootstrap.connections.erase (this);
	}
}

namespace
{
class request_response_visitor : public badem::message_visitor
{
public:
	explicit request_response_visitor (std::shared_ptr<badem::bootstrap_server> const & connection_a) :
	connection (connection_a)
	{
	}
	virtual ~request_response_visitor () = default;
	void keepalive (badem::keepalive const & message_a) override
	{
		connection->finish_request_async ();
		auto connection_l (connection->shared_from_this ());
		connection->node->background ([connection_l, message_a]() {
			connection_l->node->network.tcp_channels.process_keepalive (message_a, connection_l->remote_endpoint);
		});
	}
	void publish (badem::publish const & message_a) override
	{
		connection->finish_request_async ();
		auto connection_l (connection->shared_from_this ());
		connection->node->background ([connection_l, message_a]() {
			connection_l->node->network.tcp_channels.process_message (message_a, connection_l->remote_endpoint, connection_l->remote_node_id, connection_l->socket, connection_l->type);
		});
	}
	void confirm_req (badem::confirm_req const & message_a) override
	{
		connection->finish_request_async ();
		auto connection_l (connection->shared_from_this ());
		connection->node->background ([connection_l, message_a]() {
			connection_l->node->network.tcp_channels.process_message (message_a, connection_l->remote_endpoint, connection_l->remote_node_id, connection_l->socket, connection_l->type);
		});
	}
	void confirm_ack (badem::confirm_ack const & message_a) override
	{
		connection->finish_request_async ();
		auto connection_l (connection->shared_from_this ());
		connection->node->background ([connection_l, message_a]() {
			connection_l->node->network.tcp_channels.process_message (message_a, connection_l->remote_endpoint, connection_l->remote_node_id, connection_l->socket, connection_l->type);
		});
	}
	void bulk_pull (badem::bulk_pull const &) override
	{
		auto response (std::make_shared<badem::bulk_pull_server> (connection, std::unique_ptr<badem::bulk_pull> (static_cast<badem::bulk_pull *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	void bulk_pull_account (badem::bulk_pull_account const &) override
	{
		auto response (std::make_shared<badem::bulk_pull_account_server> (connection, std::unique_ptr<badem::bulk_pull_account> (static_cast<badem::bulk_pull_account *> (connection->requests.front ().release ()))));
		response->send_frontier ();
	}
	void bulk_push (badem::bulk_push const &) override
	{
		auto response (std::make_shared<badem::bulk_push_server> (connection));
		response->throttled_receive ();
	}
	void frontier_req (badem::frontier_req const &) override
	{
		auto response (std::make_shared<badem::frontier_req_server> (connection, std::unique_ptr<badem::frontier_req> (static_cast<badem::frontier_req *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	void node_id_handshake (badem::node_id_handshake const & message_a) override
	{
		if (connection->node->config.logging.network_node_id_handshake_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Received node_id_handshake message from %1%") % connection->remote_endpoint));
		}
		if (message_a.query)
		{
			boost::optional<std::pair<badem::account, badem::signature>> response (std::make_pair (connection->node->node_id.pub, badem::sign_message (connection->node->node_id.prv, connection->node->node_id.pub, *message_a.query)));
			assert (!badem::validate_message (response->first, *message_a.query, response->second));
			auto cookie (connection->node->network.syn_cookies.assign (badem::transport::map_tcp_to_endpoint (connection->remote_endpoint)));
			badem::node_id_handshake response_message (cookie, response);
			auto shared_const_buffer = response_message.to_shared_const_buffer ();
			// clang-format off
			connection->socket->async_write (shared_const_buffer, [connection = std::weak_ptr<badem::bootstrap_server> (connection) ](boost::system::error_code const & ec, size_t size_a) {
				if (auto connection_l = connection.lock ())
				{
					if (ec)
					{
						if (connection_l->node->config.logging.network_node_id_handshake_logging ())
						{
							connection_l->node->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % connection_l->remote_endpoint % ec.message ()));
						}
						// Stop invalid handshake
						connection_l->stop ();
					}
					else
					{
						connection_l->node->stats.inc (badem::stat::type::message, badem::stat::detail::node_id_handshake, badem::stat::dir::out);
						connection_l->finish_request ();
					}
				}
			});
			// clang-format on
		}
		else if (message_a.response)
		{
			badem::account const & node_id (message_a.response->first);
			if (!connection->node->network.syn_cookies.validate (badem::transport::map_tcp_to_endpoint (connection->remote_endpoint), node_id, message_a.response->second) && node_id != connection->node->node_id.pub)
			{
				connection->remote_node_id = node_id;
				connection->type = badem::bootstrap_server_type::realtime;
				++connection->node->bootstrap.realtime_count;
				connection->finish_request_async ();
			}
			else
			{
				// Stop invalid handshake
				connection->stop ();
			}
		}
		else
		{
			connection->finish_request_async ();
		}
		badem::account node_id (connection->remote_node_id);
		badem::bootstrap_server_type type (connection->type);
		assert (node_id.is_zero () || type == badem::bootstrap_server_type::realtime);
		auto connection_l (connection->shared_from_this ());
		connection->node->background ([connection_l, message_a, node_id, type]() {
			connection_l->node->network.tcp_channels.process_message (message_a, connection_l->remote_endpoint, node_id, connection_l->socket, type);
		});
	}
	std::shared_ptr<badem::bootstrap_server> connection;
};
}

void badem::bootstrap_server::run_next ()
{
	assert (!requests.empty ());
	request_response_visitor visitor (shared_from_this ());
	requests.front ()->visit (visitor);
}

bool badem::bootstrap_server::is_bootstrap_connection ()
{
	if (type == badem::bootstrap_server_type::undefined && !node->flags.disable_bootstrap_listener && node->bootstrap.bootstrap_count < node->config.bootstrap_connections_max)
	{
		++node->bootstrap.bootstrap_count;
		type = badem::bootstrap_server_type::bootstrap;
	}
	return type == badem::bootstrap_server_type::bootstrap;
}

bool badem::bootstrap_server::is_realtime_connection ()
{
	return type == badem::bootstrap_server_type::realtime || type == badem::bootstrap_server_type::realtime_response_server;
}
