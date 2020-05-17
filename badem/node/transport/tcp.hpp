#pragma once

#include <badem/boost/asio.hpp>
#include <badem/node/common.hpp>
#include <badem/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

namespace badem
{
class bootstrap_server;
enum class bootstrap_server_type;
namespace transport
{
	class tcp_channels;
	class channel_tcp : public badem::transport::channel
	{
		friend class badem::transport::tcp_channels;

	public:
		channel_tcp (badem::node &, std::weak_ptr<badem::socket>);
		~channel_tcp ();
		size_t hash_code () const override;
		bool operator== (badem::transport::channel const &) const override;
		void send_buffer (badem::shared_const_buffer const &, badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::function<void(boost::system::error_code const &, size_t)> tcp_callback (badem::stat::detail, badem::tcp_endpoint const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
		std::string to_string () const override;
		bool operator== (badem::transport::channel_tcp const & other_a) const
		{
			return &node == &other_a.node && socket.lock () == other_a.socket.lock ();
		}
		std::weak_ptr<badem::socket> socket;
		std::weak_ptr<badem::bootstrap_server> response_server;
		bool server{ false };

		badem::endpoint get_endpoint () const override
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			if (auto socket_l = socket.lock ())
			{
				return badem::transport::map_tcp_to_endpoint (socket_l->remote_endpoint ());
			}
			else
			{
				return badem::endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		badem::tcp_endpoint get_tcp_endpoint () const override
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			if (auto socket_l = socket.lock ())
			{
				return socket_l->remote_endpoint ();
			}
			else
			{
				return badem::tcp_endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		badem::transport::transport_type get_type () const override
		{
			return badem::transport::transport_type::tcp;
		}
	};
	class tcp_channels final
	{
		friend class badem::transport::channel_tcp;

	public:
		tcp_channels (badem::node &);
		bool insert (std::shared_ptr<badem::transport::channel_tcp>, std::shared_ptr<badem::socket>, std::shared_ptr<badem::bootstrap_server>);
		void erase (badem::tcp_endpoint const &);
		size_t size () const;
		std::shared_ptr<badem::transport::channel_tcp> find_channel (badem::tcp_endpoint const &) const;
		void random_fill (std::array<badem::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<badem::transport::channel>> random_set (size_t) const;
		bool store_all (bool = true);
		std::shared_ptr<badem::transport::channel_tcp> find_node_id (badem::account const &);
		// Get the next peer for attempting a tcp connection
		badem::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void process_message (badem::message const &, badem::tcp_endpoint const &, badem::account const &, std::shared_ptr<badem::socket>, badem::bootstrap_server_type);
		void process_keepalive (badem::keepalive const &, badem::tcp_endpoint const &);
		bool max_ip_connections (badem::tcp_endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (badem::endpoint const &);
		std::unique_ptr<seq_con_info_component> collect_seq_con_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list (std::deque<std::shared_ptr<badem::transport::channel>> &);
		void modify (std::shared_ptr<badem::transport::channel_tcp>, std::function<void(std::shared_ptr<badem::transport::channel_tcp>)>);
		void update (badem::tcp_endpoint const &);
		// Connection start
		void start_tcp (badem::endpoint const &, std::function<void(std::shared_ptr<badem::transport::channel>)> const & = nullptr);
		void start_tcp_receive_node_id (std::shared_ptr<badem::transport::channel_tcp>, badem::endpoint const &, std::shared_ptr<std::vector<uint8_t>>, std::function<void(std::shared_ptr<badem::transport::channel>)> const &);
		void udp_fallback (badem::endpoint const &, std::function<void(std::shared_ptr<badem::transport::channel>)> const &);
		void push_node_id_handshake_socket (std::shared_ptr<badem::socket> const & socket_a);
		void remove_node_id_handshake_socket (std::shared_ptr<badem::socket> const & socket_a);
		bool node_id_handhake_sockets_empty () const;
		badem::node & node;

	private:
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_sent_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class channel_tcp_wrapper final
		{
		public:
			std::shared_ptr<badem::transport::channel_tcp> channel;
			std::shared_ptr<badem::socket> socket;
			std::shared_ptr<badem::bootstrap_server> response_server;
			badem::tcp_endpoint endpoint () const
			{
				return channel->get_tcp_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_sent () const
			{
				return channel->get_last_packet_sent ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
			badem::account node_id () const
			{
				auto node_id (channel->get_node_id ());
				assert (!node_id.is_zero ());
				return node_id;
			}
		};
		class tcp_endpoint_attempt final
		{
		public:
			badem::tcp_endpoint endpoint;
			std::chrono::steady_clock::time_point last_attempt;
		};
		mutable std::mutex mutex;
		boost::multi_index_container<
		channel_tcp_wrapper,
		boost::multi_index::indexed_by<
		boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_bootstrap_attempt_tag>, boost::multi_index::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_bootstrap_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_tcp_wrapper, badem::tcp_endpoint, &channel_tcp_wrapper::endpoint>>,
		boost::multi_index::hashed_non_unique<boost::multi_index::tag<node_id_tag>, boost::multi_index::const_mem_fun<channel_tcp_wrapper, badem::account, &channel_tcp_wrapper::node_id>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_sent_tag>, boost::multi_index::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_packet_sent>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<ip_address_tag>, boost::multi_index::const_mem_fun<channel_tcp_wrapper, boost::asio::ip::address, &channel_tcp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<
		tcp_endpoint_attempt,
		boost::multi_index::indexed_by<
		boost::multi_index::hashed_unique<boost::multi_index::member<tcp_endpoint_attempt, badem::tcp_endpoint, &tcp_endpoint_attempt::endpoint>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::member<tcp_endpoint_attempt, std::chrono::steady_clock::time_point, &tcp_endpoint_attempt::last_attempt>>>>
		attempts;
		// This owns the sockets until the node_id_handshake has been completed. Needed to prevent self referencing callbacks, they are periodically removed if any are dangling.
		std::vector<std::shared_ptr<badem::socket>> node_id_handshake_sockets;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace badem
