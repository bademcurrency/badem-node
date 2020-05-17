#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/timer.hpp>
#include <badem/node/gap_cache.hpp>
#include <badem/node/repcrawler.hpp>
#include <badem/node/transport/transport.hpp>
#include <badem/secure/blockstore.hpp>
#include <badem/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace badem
{
class node;
class block;
class block_sideband;
class vote;
class election;
class transaction;

class conflict_info final
{
public:
	badem::qualified_root root;
	uint64_t difficulty;
	uint64_t adjusted_difficulty;
	std::shared_ptr<badem::election> election;
};

enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};

class election_status final
{
public:
	std::shared_ptr<badem::block> winner;
	badem::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
	unsigned confirmation_request_count;
	election_status_type type;
};

class cementable_account final
{
public:
	cementable_account (badem::account const & account_a, size_t blocks_uncemented_a);
	badem::account account;
	uint64_t blocks_uncemented{ 0 };
};

class election_timepoint final
{
public:
	std::chrono::steady_clock::time_point time;
	badem::qualified_root root;
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
public:
	explicit active_transactions (badem::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool start (std::shared_ptr<badem::block>, bool const = false, std::function<void(std::shared_ptr<badem::block>)> const & = [](std::shared_ptr<badem::block>) {});
	// clang-format on
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<badem::vote>, bool = false);
	// Is the root of this block in the roots container
	bool active (badem::block const &);
	bool active (badem::qualified_root const &);
	void update_difficulty (std::shared_ptr<badem::block>, boost::optional<badem::write_transaction const &> = boost::none);
	void adjust_difficulty (badem::block_hash const &);
	void update_active_difficulty (badem::unique_lock<std::mutex> &);
	uint64_t active_difficulty ();
	uint64_t limited_active_difficulty ();
	std::deque<std::shared_ptr<badem::block>> list_blocks (bool = false);
	void erase (badem::block const &);
	bool empty ();
	size_t size ();
	void stop ();
	bool publish (std::shared_ptr<badem::block> block_a);
	boost::optional<badem::election_status_type> confirm_block (badem::transaction const &, std::shared_ptr<badem::block>);
	void post_confirmation_height_set (badem::transaction const & transaction_a, std::shared_ptr<badem::block> block_a, badem::block_sideband const & sideband_a, badem::election_status_type election_status_type_a);
	boost::multi_index_container<
	badem::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<
	boost::multi_index::member<badem::conflict_info, badem::qualified_root, &badem::conflict_info::root>>,
	boost::multi_index::ordered_non_unique<
	boost::multi_index::member<badem::conflict_info, uint64_t, &badem::conflict_info::adjusted_difficulty>,
	std::greater<uint64_t>>>>
	roots;
	std::unordered_map<badem::block_hash, std::shared_ptr<badem::election>> blocks;
	std::deque<badem::election_status> list_confirmed ();
	std::deque<badem::election_status> confirmed;
	void add_confirmed (badem::election_status const &, badem::qualified_root const &);
	void add_inactive_votes_cache (badem::block_hash const &, badem::account const &);
	badem::gap_information find_inactive_votes_cache (badem::block_hash const &);
	badem::node & node;
	std::mutex mutex;
	std::chrono::seconds const long_election_threshold;
	// Delay until requesting confirmation for an election
	std::chrono::milliseconds const election_request_delay;
	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;
	static size_t constexpr max_block_broadcasts = 30;
	static size_t constexpr max_confirm_representatives = 30;
	static size_t constexpr max_confirm_req_batches = 20;
	static size_t constexpr max_confirm_req = 5;
	boost::circular_buffer<double> multipliers_cb;
	uint64_t trended_active_difficulty;
	size_t priority_cementable_frontiers_size ();
	size_t priority_wallet_cementable_frontiers_size ();
	boost::circular_buffer<double> difficulty_trend ();
	size_t inactive_votes_cache_size ();
	std::unordered_map<badem::block_hash, std::shared_ptr<badem::election>> pending_conf_height;
	void clear_block (badem::block_hash const & hash_a);
	void add_dropped_elections_cache (badem::qualified_root const &);
	std::chrono::steady_clock::time_point find_dropped_elections_cache (badem::qualified_root const &);
	size_t dropped_elections_cache_size ();

private:
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool add (std::shared_ptr<badem::block>, bool const = false, std::function<void(std::shared_ptr<badem::block>)> const & = [](std::shared_ptr<badem::block>) {});
	// clang-format on
	void request_loop ();
	void search_frontiers (badem::transaction const &);
	void election_escalate (std::shared_ptr<badem::election> &, badem::transaction const &, size_t const &);
	void election_broadcast (std::shared_ptr<badem::election> &, badem::transaction const &, std::deque<std::shared_ptr<badem::block>> &, std::unordered_set<badem::qualified_root> &, badem::qualified_root &);
	bool election_request_confirm (std::shared_ptr<badem::election> &, std::vector<badem::representative> const &, size_t const &,
	std::deque<std::pair<std::shared_ptr<badem::block>, std::shared_ptr<std::vector<std::shared_ptr<badem::transport::channel>>>>> & single_confirm_req_bundle_l,
	std::unordered_map<std::shared_ptr<badem::transport::channel>, std::deque<std::pair<badem::block_hash, badem::root>>> & batched_confirm_req_bundle_l);
	void request_confirm (badem::unique_lock<std::mutex> &);
	badem::account next_frontier_account{ 0 };
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	badem::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };
	unsigned ongoing_broadcasts{ 0 };
	using ordered_elections_timepoint = boost::multi_index_container<
	badem::election_timepoint,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::election_timepoint, std::chrono::steady_clock::time_point, &badem::election_timepoint::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::election_timepoint, badem::qualified_root, &badem::election_timepoint::root>>>>;
	ordered_elections_timepoint confirmed_set;
	void prioritize_frontiers_for_confirmation (badem::transaction const &, std::chrono::milliseconds, std::chrono::milliseconds);
	using prioritize_num_uncemented = boost::multi_index_container<
	badem::cementable_account,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<
	boost::multi_index::member<badem::cementable_account, badem::account, &badem::cementable_account::account>>,
	boost::multi_index::ordered_non_unique<
	boost::multi_index::member<badem::cementable_account, uint64_t, &badem::cementable_account::blocks_uncemented>,
	std::greater<uint64_t>>>>;
	prioritize_num_uncemented priority_wallet_cementable_frontiers;
	prioritize_num_uncemented priority_cementable_frontiers;
	std::unordered_set<badem::wallet_id> wallet_ids_already_iterated;
	std::unordered_map<badem::wallet_id, badem::account> next_wallet_id_accounts;
	bool skip_wallets{ false };
	void prioritize_account_for_confirmation (prioritize_num_uncemented &, size_t &, badem::account const &, badem::account_info const &, uint64_t);
	static size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static size_t constexpr confirmed_frontiers_max_pending_cut_off{ 1000 };
	boost::multi_index_container<
	badem::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::gap_information, std::chrono::steady_clock::time_point, &badem::gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::gap_information, badem::block_hash, &badem::gap_information::hash>>>>
	inactive_votes_cache;
	static size_t constexpr inactive_votes_cache_max{ 16 * 1024 };
	ordered_elections_timepoint dropped_elections_cache;
	static size_t constexpr dropped_elections_cache_max{ 32 * 1024 };
	boost::thread thread;

	friend class confirmation_height_prioritize_frontiers_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class confirmation_height_many_accounts_single_confirmation_Test;
	friend class confirmation_height_many_accounts_many_confirmations_Test;
	friend class confirmation_height_long_chains_Test;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name);
}
