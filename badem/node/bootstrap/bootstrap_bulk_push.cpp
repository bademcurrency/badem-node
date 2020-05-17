#include <badem/node/bootstrap/bootstrap.hpp>
#include <badem/node/bootstrap/bootstrap_bulk_push.hpp>
#include <badem/node/node.hpp>
#include <badem/node/transport/tcp.hpp>

badem::bulk_push_client::bulk_push_client (std::shared_ptr<badem::bootstrap_client> const & connection_a) :
connection (connection_a)
{
}

badem::bulk_push_client::~bulk_push_client ()
{
}

void badem::bulk_push_client::start ()
{
	badem::bulk_push message;
	auto this_l (shared_from_this ());
	connection->channel->send (
	message, [this_l](boost::system::error_code const & ec, size_t size_a) {
		auto transaction (this_l->connection->node->store.tx_begin_read ());
		if (!ec)
		{
			this_l->push (transaction);
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				this_l->connection->node->logger.try_log (boost::str (boost::format ("Unable to send bulk_push request: %1%") % ec.message ()));
			}
		}
	},
	false); // is bootstrap traffic is_droppable false
}

void badem::bulk_push_client::push (badem::transaction const & transaction_a)
{
	std::shared_ptr<badem::block> block;
	bool finished (false);
	while (block == nullptr && !finished)
	{
		if (current_target.first.is_zero () || current_target.first == current_target.second)
		{
			badem::lock_guard<std::mutex> guard (connection->attempt->mutex);
			if (!connection->attempt->bulk_push_targets.empty ())
			{
				current_target = connection->attempt->bulk_push_targets.back ();
				connection->attempt->bulk_push_targets.pop_back ();
			}
			else
			{
				finished = true;
			}
		}
		if (!finished)
		{
			block = connection->node->store.block_get (transaction_a, current_target.first);
			if (block == nullptr)
			{
				current_target.first = badem::block_hash (0);
			}
			else
			{
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					connection->node->logger.try_log ("Bulk pushing range ", current_target.first.to_string (), " down to ", current_target.second.to_string ());
				}
			}
		}
	}
	if (finished)
	{
		send_finished ();
	}
	else
	{
		current_target.first = block->previous ();
		push_block (*block);
	}
}

void badem::bulk_push_client::send_finished ()
{
	badem::shared_const_buffer buffer (static_cast<uint8_t> (badem::block_type::not_a_block));
	auto this_l (shared_from_this ());
	connection->channel->send_buffer (buffer, badem::stat::detail::all, [this_l](boost::system::error_code const & ec, size_t size_a) {
		try
		{
			this_l->promise.set_value (false);
		}
		catch (std::future_error &)
		{
		}
	});
}

void badem::bulk_push_client::push_block (badem::block const & block_a)
{
	std::vector<uint8_t> buffer;
	{
		badem::vectorstream stream (buffer);
		badem::serialize_block (stream, block_a);
	}
	auto this_l (shared_from_this ());
	connection->channel->send_buffer (badem::shared_const_buffer (std::move (buffer)), badem::stat::detail::all, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			auto transaction (this_l->connection->node->store.tx_begin_read ());
			this_l->push (transaction);
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				this_l->connection->node->logger.try_log (boost::str (boost::format ("Error sending block during bulk push: %1%") % ec.message ()));
			}
		}
	});
}

badem::bulk_push_server::bulk_push_server (std::shared_ptr<badem::bootstrap_server> const & connection_a) :
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
connection (connection_a)
{
	receive_buffer->resize (256);
}

void badem::bulk_push_server::throttled_receive ()
{
	if (!connection->node->block_processor.half_full ())
	{
		receive ();
	}
	else
	{
		auto this_l (shared_from_this ());
		connection->node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_l]() {
			if (!this_l->connection->stopped)
			{
				this_l->throttled_receive ();
			}
		});
	}
}

void badem::bulk_push_server::receive ()
{
	if (connection->node->bootstrap_initiator.in_progress ())
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log ("Aborting bulk_push because a bootstrap attempt is in progress");
		}
	}
	else
	{
		auto this_l (shared_from_this ());
		connection->socket->async_read (receive_buffer, 1, [this_l](boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				this_l->received_type ();
			}
			else
			{
				if (this_l->connection->node->config.logging.bulk_pull_logging ())
				{
					this_l->connection->node->logger.try_log (boost::str (boost::format ("Error receiving block type: %1%") % ec.message ()));
				}
			}
		});
	}
}

void badem::bulk_push_server::received_type ()
{
	auto this_l (shared_from_this ());
	badem::block_type type (static_cast<badem::block_type> (receive_buffer->data ()[0]));
	switch (type)
	{
		case badem::block_type::send:
		{
			connection->node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::send, badem::stat::dir::in);
			connection->socket->async_read (receive_buffer, badem::send_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case badem::block_type::receive:
		{
			connection->node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::receive, badem::stat::dir::in);
			connection->socket->async_read (receive_buffer, badem::receive_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case badem::block_type::open:
		{
			connection->node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::open, badem::stat::dir::in);
			connection->socket->async_read (receive_buffer, badem::open_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case badem::block_type::change:
		{
			connection->node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::change, badem::stat::dir::in);
			connection->socket->async_read (receive_buffer, badem::change_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case badem::block_type::state:
		{
			connection->node->stats.inc (badem::stat::type::bootstrap, badem::stat::detail::state_block, badem::stat::dir::in);
			connection->socket->async_read (receive_buffer, badem::state_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case badem::block_type::not_a_block:
		{
			connection->finish_request ();
			break;
		}
		default:
		{
			if (connection->node->config.logging.network_packet_logging ())
			{
				connection->node->logger.try_log ("Unknown type received as block type");
			}
			break;
		}
	}
}

void badem::bulk_push_server::received_block (boost::system::error_code const & ec, size_t size_a, badem::block_type type_a)
{
	if (!ec)
	{
		badem::bufferstream stream (receive_buffer->data (), size_a);
		auto block (badem::deserialize_block (stream, type_a));
		if (block != nullptr && !badem::work_validate (*block))
		{
			connection->node->process_active (std::move (block));
			throttled_receive ();
		}
		else
		{
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				connection->node->logger.try_log ("Error deserializing block received from pull request");
			}
		}
	}
}
