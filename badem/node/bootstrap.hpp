#pragma once

#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>
#include <badem/secure/blockstore.hpp>
#include <badem/secure/ledger.hpp>

#include <boost/log/sources/logger.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

namespace badem
{
class bootstrap_attempt;
class bootstrap_client;
class node;
namespace transport
{
	class channel_tcp;
}
enum class sync_result
{
	success,
	error,
	fork
};

class bootstrap_client;
class pull_info
{
public:
	using count_t = badem::bulk_pull::count_t;
	pull_info () = default;
	pull_info (badem::account const &, badem::block_hash const &, badem::block_hash const &, count_t = 0);
	badem::account account{ 0 };
	badem::block_hash head{ 0 };
	badem::block_hash head_original{ 0 };
	badem::block_hash end{ 0 };
	count_t count{ 0 };
	unsigned attempts{ 0 };
	uint64_t processed{ 0 };
};
enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
class frontier_req_client;
class bulk_push_client;
class bulk_pull_account_client;
class bootstrap_attempt final : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<badem::node> node_a, badem::bootstrap_mode mode_a = badem::bootstrap_mode::legacy);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<badem::bootstrap_client> connection (std::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (std::unique_lock<std::mutex> &);
	void request_pull (std::unique_lock<std::mutex> &);
	void request_push (std::unique_lock<std::mutex> &);
	void add_connection (badem::endpoint const &);
	void connect_client (badem::tcp_endpoint const &);
	void pool_connection (std::shared_ptr<badem::bootstrap_client>);
	void stop ();
	void requeue_pull (badem::pull_info const &);
	void add_pull (badem::pull_info const &);
	bool still_pulling ();
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (badem::block_hash const &, badem::block_hash const &);
	bool process_block (std::shared_ptr<badem::block>, badem::account const &, uint64_t, bool);
	void lazy_run ();
	void lazy_start (badem::block_hash const &);
	void lazy_add (badem::block_hash const &);
	bool lazy_finished ();
	void lazy_pull_flush ();
	void lazy_clear ();
	void request_pending (std::unique_lock<std::mutex> &);
	void requeue_pending (badem::account const &);
	void wallet_run ();
	void wallet_start (std::deque<badem::account> &);
	bool wallet_finished ();
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::weak_ptr<badem::bootstrap_client>> clients;
	std::weak_ptr<badem::bootstrap_client> connection_frontier_request;
	std::weak_ptr<badem::frontier_req_client> frontiers;
	std::weak_ptr<badem::bulk_push_client> push;
	std::deque<badem::pull_info> pulls;
	std::deque<std::shared_ptr<badem::bootstrap_client>> idle;
	std::atomic<unsigned> connections;
	std::atomic<unsigned> pulling;
	std::shared_ptr<badem::node> node;
	std::atomic<unsigned> account_count;
	std::atomic<uint64_t> total_blocks;
	std::atomic<unsigned> runs_count;
	std::vector<std::pair<badem::block_hash, badem::block_hash>> bulk_push_targets;
	std::atomic<bool> stopped;
	badem::bootstrap_mode mode;
	std::mutex mutex;
	std::condition_variable condition;
	// Lazy bootstrap
	std::unordered_set<badem::block_hash> lazy_blocks;
	std::unordered_map<badem::block_hash, std::pair<badem::block_hash, badem::uint128_t>> lazy_state_unknown;
	std::unordered_map<badem::block_hash, badem::uint128_t> lazy_balances;
	std::unordered_set<badem::block_hash> lazy_keys;
	std::deque<badem::block_hash> lazy_pulls;
	std::atomic<uint64_t> lazy_stopped;
	uint64_t lazy_max_stopped = 256;
	std::mutex lazy_mutex;
	// Wallet lazy bootstrap
	std::deque<badem::account> wallet_accounts;
};
class frontier_req_client final : public std::enable_shared_from_this<badem::frontier_req_client>
{
public:
	explicit frontier_req_client (std::shared_ptr<badem::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void unsynced (badem::block_hash const &, badem::block_hash const &);
	void next (badem::transaction const &);
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::account current;
	badem::block_hash frontier;
	unsigned count;
	badem::account landing;
	badem::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
	std::deque<std::pair<badem::account, badem::block_hash>> accounts;
	static size_t constexpr size_frontier = sizeof (badem::account) + sizeof (badem::block_hash);
};
class bulk_pull_client final : public std::enable_shared_from_this<badem::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<badem::bootstrap_client>, badem::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, badem::block_type);
	badem::block_hash first ();
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::block_hash expected;
	badem::account known_account;
	badem::pull_info pull;
	uint64_t pull_blocks;
	uint64_t unexpected_count;
};
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<badem::node>, std::shared_ptr<badem::bootstrap_attempt>, std::shared_ptr<badem::transport::channel_tcp>);
	~bootstrap_client ();
	std::shared_ptr<badem::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<badem::node> node;
	std::shared_ptr<badem::bootstrap_attempt> attempt;
	std::shared_ptr<badem::transport::channel_tcp> channel;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
};
class bulk_push_client final : public std::enable_shared_from_this<badem::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<badem::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (badem::transaction const &);
	void push_block (badem::block const &);
	void send_finished ();
	std::shared_ptr<badem::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<badem::block_hash, badem::block_hash> current_target;
};
class bulk_pull_account_client final : public std::enable_shared_from_this<badem::bulk_pull_account_client>
{
public:
	bulk_pull_account_client (std::shared_ptr<badem::bootstrap_client>, badem::account const &);
	~bulk_pull_account_client ();
	void request ();
	void receive_pending ();
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::account account;
	uint64_t pull_blocks;
};
class cached_pulls final
{
public:
	std::chrono::steady_clock::time_point time;
	badem::uint512_union account_head;
	badem::block_hash new_head;
};
class pulls_cache final
{
public:
	void add (badem::pull_info const &);
	void update_pull (badem::pull_info &);
	void remove (badem::pull_info const &);
	std::mutex pulls_cache_mutex;
	class account_head_tag
	{
	};
	boost::multi_index_container<
	badem::cached_pulls,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::cached_pulls, std::chrono::steady_clock::time_point, &badem::cached_pulls::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::tag<account_head_tag>, boost::multi_index::member<badem::cached_pulls, badem::uint512_union, &badem::cached_pulls::account_head>>>>
	cache;
	constexpr static size_t cache_size_max = 10000;
};

class bootstrap_initiator final
{
public:
	explicit bootstrap_initiator (badem::node &);
	~bootstrap_initiator ();
	void bootstrap (badem::endpoint const &, bool add_to_peers = true);
	void bootstrap ();
	void bootstrap_lazy (badem::block_hash const &, bool = false);
	void bootstrap_wallet (std::deque<badem::account> &);
	void run_bootstrap ();
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<badem::bootstrap_attempt> current_attempt ();
	badem::pulls_cache cache;
	void stop ();

private:
	badem::node & node;
	std::shared_ptr<badem::bootstrap_attempt> attempt;
	std::atomic<bool> stopped;
	std::mutex mutex;
	std::condition_variable condition;
	std::mutex observers_mutex;
	std::vector<std::function<void(bool)>> observers;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);

class bootstrap_server;
class bootstrap_listener final
{
public:
	bootstrap_listener (uint16_t, badem::node &);
	void start ();
	void stop ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<badem::socket>);
	size_t connection_count ();

	std::mutex mutex;
	std::unordered_map<badem::bootstrap_server *, std::weak_ptr<badem::bootstrap_server>> connections;
	badem::tcp_endpoint endpoint ();
	badem::node & node;
	std::shared_ptr<badem::server_socket> listening_socket;
	bool on;
	std::atomic<size_t> bootstrap_count{ 0 };
	std::atomic<size_t> realtime_count{ 0 };

private:
	uint16_t port;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_listener & bootstrap_listener, const std::string & name);

class message;
enum class bootstrap_server_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};
class bootstrap_server final : public std::enable_shared_from_this<badem::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<badem::socket>, std::shared_ptr<badem::node>);
	~bootstrap_server ();
	void stop ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_bulk_pull_account_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_frontier_req_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_keepalive_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_publish_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_confirm_req_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_confirm_ack_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_node_id_handshake_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void add_request (std::unique_ptr<badem::message>);
	void finish_request ();
	void finish_request_async ();
	void run_next ();
	void timeout ();
	bool is_bootstrap_connection ();
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<badem::socket> socket;
	std::shared_ptr<badem::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<badem::message>> requests;
	std::atomic<bool> stopped{ false };
	std::atomic<badem::bootstrap_server_type> type{ badem::bootstrap_server_type::undefined };
	std::atomic<bool> keepalive_first{ true };
	// Remote enpoint used to remove response channel even after socket closing
	badem::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	badem::account remote_node_id{ 0 };
};
class bulk_pull;
class bulk_pull_server final : public std::enable_shared_from_this<badem::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::bulk_pull>);
	void set_current_end ();
	std::shared_ptr<badem::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<badem::bootstrap_server> connection;
	std::unique_ptr<badem::bulk_pull> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	badem::block_hash current;
	bool include_start;
	badem::bulk_pull::count_t max_count;
	badem::bulk_pull::count_t sent_count;
};
class bulk_pull_account;
class bulk_pull_account_server final : public std::enable_shared_from_this<badem::bulk_pull_account_server>
{
public:
	bulk_pull_account_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::bulk_pull_account>);
	void set_params ();
	std::pair<std::unique_ptr<badem::pending_key>, std::unique_ptr<badem::pending_info>> get_next ();
	void send_frontier ();
	void send_next_block ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void complete (boost::system::error_code const &, size_t);
	std::shared_ptr<badem::bootstrap_server> connection;
	std::unique_ptr<badem::bulk_pull_account> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	std::unordered_set<badem::uint256_union> deduplication;
	badem::pending_key current_key;
	bool pending_address_only;
	bool pending_include_address;
	bool invalid_request;
};
class bulk_push_server final : public std::enable_shared_from_this<badem::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<badem::bootstrap_server> const &);
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, badem::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<badem::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server final : public std::enable_shared_from_this<badem::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::frontier_req>);
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<badem::bootstrap_server> connection;
	badem::account current;
	badem::block_hash frontier;
	std::unique_ptr<badem::frontier_req> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	size_t count;
	std::deque<std::pair<badem::account, badem::block_hash>> accounts;
};
}
