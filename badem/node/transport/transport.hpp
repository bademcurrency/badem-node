#pragma once

#include <badem/lib/stats.hpp>
#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>

#include <unordered_set>

namespace badem
{
class bandwidth_limiter final
{
public:
	// initialize with rate 0 = unbounded
	bandwidth_limiter (const size_t);
	bool should_drop (const size_t &);
	size_t get_rate ();

private:
	//last time rate was adjusted
	std::chrono::steady_clock::time_point next_trend;
	//trend rate over 20 poll periods
	boost::circular_buffer<size_t> rate_buffer{ 20, 0 };
	//limit bandwidth to
	const size_t limit;
	//rate, increment if message_size + rate < rate
	size_t rate;
	//trended rate to even out spikes in traffic
	size_t trended_rate;
	std::mutex mutex;
};
namespace transport
{
	class message;
	badem::endpoint map_endpoint_to_v6 (badem::endpoint const &);
	badem::endpoint map_tcp_to_endpoint (badem::tcp_endpoint const &);
	badem::tcp_endpoint map_endpoint_to_tcp (badem::endpoint const &);
	// Unassigned, reserved, self
	bool reserved_address (badem::endpoint const &, bool = false);
	// Maximum number of peers per IP
	static size_t constexpr max_peers_per_ip = 10;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2
	};
	class channel
	{
	public:
		channel (badem::node &);
		virtual ~channel () = default;
		virtual size_t hash_code () const = 0;
		virtual bool operator== (badem::transport::channel const &) const = 0;
		void send (badem::message const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr, bool const = true);
		virtual void send_buffer (badem::shared_const_buffer const &, badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) = 0;
		virtual std::function<void(boost::system::error_code const &, size_t)> callback (badem::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const = 0;
		virtual std::string to_string () const = 0;
		virtual badem::endpoint get_endpoint () const = 0;
		virtual badem::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual badem::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<badem::account> get_node_id_optional () const
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			return node_id;
		}

		badem::account get_node_id () const
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			if (node_id.is_initialized ())
			{
				return node_id.get ();
			}
			else
			{
				return 0;
			}
		}

		void set_node_id (badem::account node_id_a)
		{
			badem::lock_guard<std::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		uint8_t get_network_version () const
		{
			return network_version;
		}

		void set_network_version (uint8_t network_version_a)
		{
			network_version = network_version_a;
		}

		mutable std::mutex channel_mutex;
		badem::bandwidth_limiter limiter;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::time_point () };
		boost::optional<badem::account> node_id{ boost::none };
		std::atomic<uint8_t> network_version{ 0 };

	protected:
		badem::node & node;
	};
} // namespace transport
} // namespace badem

namespace std
{
template <>
struct hash<::badem::transport::channel>
{
	size_t operator() (::badem::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::badem::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::badem::transport::channel const> const & lhs, std::reference_wrapper<::badem::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::badem::transport::channel>
{
	size_t operator() (::badem::transport::channel const & channel_a) const
	{
		std::hash<::badem::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::badem::transport::channel const>>
{
	size_t operator() (std::reference_wrapper<::badem::transport::channel const> const & channel_a) const
	{
		std::hash<::badem::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
