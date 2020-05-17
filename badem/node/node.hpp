#pragma once

#include <badem/boost/asio.hpp>
#include <badem/lib/alarm.hpp>
#include <badem/lib/rep_weights.hpp>
#include <badem/lib/stats.hpp>
#include <badem/lib/work.hpp>
#include <badem/node/active_transactions.hpp>
#include <badem/node/blockprocessor.hpp>
#include <badem/node/bootstrap/bootstrap.hpp>
#include <badem/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <badem/node/bootstrap/bootstrap_bulk_push.hpp>
#include <badem/node/bootstrap/bootstrap_frontier.hpp>
#include <badem/node/bootstrap/bootstrap_server.hpp>
#include <badem/node/confirmation_height_processor.hpp>
#include <badem/node/distributed_work.hpp>
#include <badem/node/election.hpp>
#include <badem/node/gap_cache.hpp>
#include <badem/node/logging.hpp>
#include <badem/node/network.hpp>
#include <badem/node/node_observers.hpp>
#include <badem/node/nodeconfig.hpp>
#include <badem/node/online_reps.hpp>
#include <badem/node/payment_observer_processor.hpp>
#include <badem/node/portmapping.hpp>
#include <badem/node/repcrawler.hpp>
#include <badem/node/signatures.hpp>
#include <badem/node/vote_processor.hpp>
#include <badem/node/wallet.hpp>
#include <badem/node/websocket.hpp>
#include <badem/node/write_database_queue.hpp>
#include <badem/secure/ledger.hpp>

#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/latch.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <queue>
#include <vector>

namespace badem
{
class node;

class work_pool;
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	badem::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (badem::block_hash const &);
	bool recent (badem::block_hash const &);
	boost::multi_index_container<
	badem::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::block_arrival_info, std::chrono::steady_clock::time_point, &badem::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::block_arrival_info, badem::block_hash, &badem::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name);

std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name);
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name);

class node final : public std::enable_shared_from_this<badem::node>
{
public:
	node (boost::asio::io_context &, uint16_t, boost::filesystem::path const &, badem::alarm &, badem::logging const &, badem::work_pool &, badem::node_flags = badem::node_flags ());
	node (boost::asio::io_context &, boost::filesystem::path const &, badem::alarm &, badem::node_config const &, badem::work_pool &, badem::node_flags = badem::node_flags ());
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.io_ctx.post (action_a);
	}
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<badem::node> shared ();
	int store_version ();
	void receive_confirmed (badem::transaction const &, std::shared_ptr<badem::block>, badem::block_hash const &);
	void process_confirmed_data (badem::transaction const &, std::shared_ptr<badem::block>, badem::block_hash const &, badem::block_sideband const &, badem::account &, badem::uint128_t &, bool &, badem::account &);
	void process_confirmed (badem::election_status const &, uint8_t = 0);
	void process_active (std::shared_ptr<badem::block>);
	badem::process_return process (badem::block const &);
	badem::process_return process_local (std::shared_ptr<badem::block>, bool const = false);
	void keepalive_preconfigured (std::vector<std::string> const &);
	badem::block_hash latest (badem::account const &);
	badem::uint128_t balance (badem::account const &);
	std::shared_ptr<badem::block> block (badem::block_hash const &);
	std::pair<badem::uint128_t, badem::uint128_t> balance_pending (badem::account const &);
	badem::uint128_t weight (badem::account const &);
	badem::block_hash rep_block (badem::account const &);
	badem::uint128_t minimum_principal_weight ();
	badem::uint128_t minimum_principal_weight (badem::uint128_t const &);
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void backup_wallet ();
	void search_pending ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	int price (badem::uint128_t const &, int);
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	boost::optional<uint64_t> work_generate_blocking (badem::block &, uint64_t);
	boost::optional<uint64_t> work_generate_blocking (badem::block &);
	boost::optional<uint64_t> work_generate_blocking (badem::root const &, uint64_t, boost::optional<badem::account> const & = boost::none);
	boost::optional<uint64_t> work_generate_blocking (badem::root const &, boost::optional<badem::account> const & = boost::none);
	void work_generate (badem::root const &, std::function<void(boost::optional<uint64_t>)>, uint64_t, boost::optional<badem::account> const & = boost::none, bool const = false);
	void work_generate (badem::root const &, std::function<void(boost::optional<uint64_t>)>, boost::optional<badem::account> const & = boost::none);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<badem::block>);
	bool block_confirmed_or_being_confirmed (badem::transaction const &, badem::block_hash const &);
	void process_fork (badem::transaction const &, std::shared_ptr<badem::block>);
	bool validate_block_by_previous (badem::transaction const &, std::shared_ptr<badem::block>);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string>, std::shared_ptr<std::string>, std::shared_ptr<boost::asio::ip::tcp::resolver>);
	badem::uint128_t delta () const;
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	bool online () const;
	bool init_error () const;
	badem::worker worker;
	badem::write_database_queue write_database_queue;
	boost::asio::io_context & io_ctx;
	boost::latch node_initialized_latch;
	badem::network_params network_params;
	badem::node_config config;
	badem::stat stats;
	std::shared_ptr<badem::websocket::listener> websocket_server;
	badem::node_flags flags;
	badem::alarm & alarm;
	badem::work_pool & work;
	badem::distributed_work_factory distributed_work;
	badem::logger_mt logger;
	std::unique_ptr<badem::block_store> store_impl;
	badem::block_store & store;
	std::unique_ptr<badem::wallets_store> wallets_store_impl;
	badem::wallets_store & wallets_store;
	badem::gap_cache gap_cache;
	badem::ledger ledger;
	badem::signature_checker checker;
	badem::network network;
	badem::bootstrap_initiator bootstrap_initiator;
	badem::bootstrap_listener bootstrap;
	boost::filesystem::path application_path;
	badem::node_observers observers;
	badem::port_mapping port_mapping;
	badem::vote_processor vote_processor;
	badem::rep_crawler rep_crawler;
	unsigned warmed_up;
	badem::block_processor block_processor;
	boost::thread block_processor_thread;
	badem::block_arrival block_arrival;
	badem::online_reps online_reps;
	badem::votes_cache votes_cache;
	badem::keypair node_id;
	badem::block_uniquer block_uniquer;
	badem::vote_uniquer vote_uniquer;
	badem::pending_confirmation_height pending_confirmation_height; // Used by both active and confirmation height processor
	badem::active_transactions active;
	badem::confirmation_height_processor confirmation_height_processor;
	badem::payment_observer_processor payment_observer_processor;
	badem::wallets wallets;
	const std::chrono::steady_clock::time_point startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name);

badem::node_flags const & inactive_node_flag_defaults ();

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path = badem::working_path (), uint16_t = 24000, badem::node_flags const & = badem::inactive_node_flag_defaults ());
	~inactive_node ();
	boost::filesystem::path path;
	std::shared_ptr<boost::asio::io_context> io_context;
	badem::alarm alarm;
	badem::logging logging;
	badem::work_pool work;
	uint16_t peering_port;
	std::shared_ptr<badem::node> node;
};
}
