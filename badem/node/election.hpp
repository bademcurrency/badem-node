#pragma once

#include <badem/node/active_transactions.hpp>
#include <badem/secure/blockstore.hpp>
#include <badem/secure/common.hpp>
#include <badem/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace badem
{
class channel;
class node;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	badem::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
class election final : public std::enable_shared_from_this<badem::election>
{
	std::function<void(std::shared_ptr<badem::block>)> confirmation_action;

public:
	election (badem::node &, std::shared_ptr<badem::block>, bool const, std::function<void(std::shared_ptr<badem::block>)> const &);
	badem::election_vote_result vote (badem::account, uint64_t, badem::block_hash);
	badem::tally_t tally ();
	// Check if we have vote quorum
	bool have_quorum (badem::tally_t const &, badem::uint128_t) const;
	// Change our winner to agree with the network
	void compute_rep_votes (badem::transaction const &);
	void confirm_once (badem::election_status_type = badem::election_status_type::active_confirmed_quorum);
	// Confirm this block if quorum is met
	void confirm_if_quorum ();
	void log_votes (badem::tally_t const &) const;
	bool publish (std::shared_ptr<badem::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void clear_dependent ();
	void clear_blocks ();
	void insert_inactive_votes_cache ();
	void stop ();
	badem::node & node;
	std::unordered_map<badem::account, badem::vote_info> last_votes;
	std::unordered_map<badem::block_hash, std::shared_ptr<badem::block>> blocks;
	std::chrono::steady_clock::time_point election_start;
	badem::election_status status;
	bool skip_delay;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<badem::block_hash, badem::uint128_t> last_tally;
	unsigned confirmation_request_count{ 0 };
	std::unordered_set<badem::block_hash> dependent_blocks;
	std::chrono::seconds late_blocks_delay{ 5 };
};
}
