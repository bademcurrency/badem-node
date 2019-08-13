#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/timer.hpp>
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
	election_status_type type;
};

class cementable_account final
{
public:
	cementable_account (badem::account const & account_a, size_t blocks_uncemented_a);
	badem::account account;
	uint64_t blocks_uncemented{ 0 };
};

class confirmed_set_info final
{
public:
	std::chrono::steady_clock::time_point time;
	badem::uint512_union root;
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
	bool start (std::shared_ptr<badem::block>, std::function<void(std::shared_ptr<badem::block>)> const & = [](std::shared_ptr<badem::block>) {});
	// clang-format on
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<badem::vote>, bool = false);
	// Is the root of this block in the roots container
	bool active (badem::block const &);
	bool active (badem::qualified_root const &);
	void update_difficulty (badem::block const &);
	void adjust_difficulty (badem::block_hash const &);
	void update_active_difficulty (std::unique_lock<std::mutex> &);
	uint64_t active_difficulty ();
	std::deque<std::shared_ptr<badem::block>> list_blocks (bool = false);
	void erase (badem::block const &);
	//drop 2 from roots based on adjusted_difficulty
	void flush_lowest ();
	bool empty ();
	size_t size ();
	void stop ();
	bool publish (std::shared_ptr<badem::block> block_a);
	void confirm_block (badem::transaction const &, std::shared_ptr<badem::block>, badem::block_sideband const &);
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
	badem::node & node;
	std::mutex mutex;
	// Minimum number of confirmation requests
	static unsigned constexpr minimum_confirmation_request_count = 2;
	// Threshold for considering confirmation request count high
	static unsigned constexpr high_confirmation_request_count = 2;
	size_t long_unconfirmed_size = 0;
	static size_t constexpr max_broadcast_queue = 1000;
	boost::circular_buffer<double> multipliers_cb;
	uint64_t trended_active_difficulty;
	size_t priority_cementable_frontiers_size ();
	size_t priority_wallet_cementable_frontiers_size ();
	boost::circular_buffer<double> difficulty_trend ();

private:
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool add (std::shared_ptr<badem::block>, std::function<void(std::shared_ptr<badem::block>)> const & = [](std::shared_ptr<badem::block>) {});
	// clang-format on
	void request_loop ();
	void request_confirm (std::unique_lock<std::mutex> &);
	void confirm_frontiers (badem::transaction const &);
	badem::account next_frontier_account{ 0 };
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	std::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };
	boost::multi_index_container<
	badem::confirmed_set_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::confirmed_set_info, std::chrono::steady_clock::time_point, &badem::confirmed_set_info::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::confirmed_set_info, badem::qualified_root, &badem::confirmed_set_info::root>>>>
	confirmed_set;
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
	std::unordered_set<badem::account> wallet_accounts_already_iterated;
	std::unordered_map<badem::uint256_union, badem::account> next_wallet_frontier_accounts;
	bool frontiers_fully_confirmed{ false };
	bool skip_wallets{ false };
	void prioritize_account_for_confirmation (prioritize_num_uncemented &, size_t &, badem::account const &, badem::account_info const &, uint64_t);
	static size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static size_t constexpr confirmed_frontiers_max_pending_cut_off{ 1000 };
	boost::thread thread;

	friend class confirmation_height_prioritize_frontiers_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name);
}
