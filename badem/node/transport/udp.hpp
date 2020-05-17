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

#include <mutex>

namespace badem
{
class message_buffer;
namespace transport
{
	class udp_channels;
	class channel_udp final : public badem::transport::channel
	{
		friend class badem::transport::udp_channels;

	public:
		channel_udp (badem::transport::udp_channels &, badem::endpoint const &, uint8_t protocol_version);
		size_t hash_code () const override;
		bool operator== (badem::transport::channel const &) const override;
		void send_buffer (badem::shared_const_buffer const &, badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (badem::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}

		badem::endpoint get_endpoint () const override
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return endpoint;
		}

		badem::tcp_endpoint get_tcp_endpoint () const override
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return badem::transport::map_endpoint_to_tcp (endpoint);
		}

		badem::transport::transport_type get_type () const override
		{
			return badem::transport::transport_type::udp;
		}

	private:
		badem::endpoint endpoint;
		badem::transport::udp_channels & channels;
	};
	class udp_channels final
	{
		friend class badem::transport::channel_udp;

	public:
		udp_channels (badem::node &, uint16_t);
		std::shared_ptr<badem::transport::channel_udp> insert (badem::endpoint const &, unsigned);
		void erase (badem::endpoint const &);
		size_t size () const;
		std::shared_ptr<badem::transport::channel_udp> channel (badem::endpoint const &) const;
		void random_fill (std::array<badem::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<badem::transport::channel>> random_set (size_t) const;
		bool store_all (bool = true);
		std::shared_ptr<badem::transport::channel_udp> find_node_id (badem::account const &);
		void clean_node_id (badem::account const &);
		void clean_node_id (badem::endpoint const &, badem::account const &);
		// Get the next peer for attempting a tcp bootstrap connection
		badem::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void send (badem::shared_const_buffer const & buffer_a, badem::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a);
		badem::endpoint get_local_endpoint () const;
		void receive_action (badem::message_buffer *);
		void process_packets ();
		std::shared_ptr<badem::transport::channel> create (badem::endpoint const &);
		bool max_ip_connections (badem::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (badem::endpoint const &);
		std::unique_ptr<seq_con_info_component> collect_seq_con_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list (std::deque<std::shared_ptr<badem::transport::channel>> &);
		void modify (std::shared_ptr<badem::transport::channel_udp>, std::function<void(std::shared_ptr<badem::transport::channel_udp>)>);
		badem::node & node;

	private:
		void close_socket ();
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_received_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<badem::transport::channel_udp> channel;
			badem::endpoint endpoint () const
			{
				return channel->get_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->get_last_packet_received ();
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
				return channel->get_node_id ();
			}
		};
		class endpoint_attempt final
		{
		public:
			badem::endpoint endpoint;
			std::chrono::steady_clock::time_point last_attempt;
		};
		mutable std::mutex mutex;
		boost::multi_index_container<
		channel_udp_wrapper,
		boost::multi_index::indexed_by<
		boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_bootstrap_attempt_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_bootstrap_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, badem::endpoint, &channel_udp_wrapper::endpoint>>,
		boost::multi_index::hashed_non_unique<boost::multi_index::tag<node_id_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, badem::account, &channel_udp_wrapper::node_id>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_received_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<ip_address_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<
		endpoint_attempt,
		boost::multi_index::indexed_by<
		boost::multi_index::hashed_unique<boost::multi_index::member<endpoint_attempt, badem::endpoint, &endpoint_attempt::endpoint>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::member<endpoint_attempt, std::chrono::steady_clock::time_point, &endpoint_attempt::last_attempt>>>>
		attempts;
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		boost::asio::ip::udp::socket socket;
		badem::endpoint local_endpoint;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace badem
