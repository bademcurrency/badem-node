#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/rep_weights.hpp>
#include <badem/secure/common.hpp>

namespace badem
{
class block_store;
class stat;

using tally_t = std::map<badem::uint128_t, std::shared_ptr<badem::block>, std::greater<badem::uint128_t>>;
class ledger final
{
public:
	ledger (badem::block_store &, badem::stat &, bool = true, bool = true);
	badem::account account (badem::transaction const &, badem::block_hash const &) const;
	badem::uint128_t amount (badem::transaction const &, badem::account const &);
	badem::uint128_t amount (badem::transaction const &, badem::block_hash const &);
	badem::uint128_t balance (badem::transaction const &, badem::block_hash const &) const;
	badem::uint128_t account_balance (badem::transaction const &, badem::account const &);
	badem::uint128_t account_pending (badem::transaction const &, badem::account const &);
	badem::uint128_t weight (badem::account const &);
	std::shared_ptr<badem::block> successor (badem::transaction const &, badem::qualified_root const &);
	std::shared_ptr<badem::block> forked_block (badem::transaction const &, badem::block const &);
	bool block_confirmed (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const;
	bool block_not_confirmed_or_not_exists (badem::block const & block_a) const;
	badem::block_hash latest (badem::transaction const &, badem::account const &);
	badem::root latest_root (badem::transaction const &, badem::account const &);
	badem::block_hash representative (badem::transaction const &, badem::block_hash const &);
	badem::block_hash representative_calculated (badem::transaction const &, badem::block_hash const &);
	bool block_exists (badem::block_hash const &);
	bool block_exists (badem::block_type, badem::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (badem::block_hash const &);
	bool is_send (badem::transaction const &, badem::state_block const &) const;
	badem::account const & block_destination (badem::transaction const &, badem::block const &);
	badem::block_hash block_source (badem::transaction const &, badem::block const &);
	badem::process_return process (badem::write_transaction const &, badem::block const &, badem::signature_verification = badem::signature_verification::unknown);
	bool rollback (badem::write_transaction const &, badem::block_hash const &, std::vector<std::shared_ptr<badem::block>> &);
	bool rollback (badem::write_transaction const &, badem::block_hash const &);
	void change_latest (badem::write_transaction const &, badem::account const &, badem::account_info const &, badem::account_info const &);
	void dump_account_chain (badem::account const &);
	bool could_fit (badem::transaction const &, badem::block const &);
	bool is_epoch_link (badem::link const &);
	badem::account const & epoch_signer (badem::link const &) const;
	badem::link const & epoch_link (badem::epoch) const;
	static badem::uint128_t const unit;
	badem::network_params network_params;
	badem::block_store & store;
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count_cache{ 0 };
	badem::rep_weights rep_weights;
	badem::stat & stats;
	std::unordered_map<badem::account, badem::uint128_t> bootstrap_weights;
	std::atomic<size_t> bootstrap_weights_size{ 0 };
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name);
}
