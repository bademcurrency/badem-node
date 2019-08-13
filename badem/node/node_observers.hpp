#pragma once

#include <badem/lib/blocks.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>
#include <badem/node/active_transactions.hpp>
#include <badem/node/transport/transport.hpp>
#include <badem/secure/blockstore.hpp>

namespace badem
{
class node_observers final
{
public:
	using blocks_t = badem::observer_set<badem::election_status const &, badem::account const &, badem::uint128_t const &, bool>;
	blocks_t blocks;
	badem::observer_set<bool> wallet;
	badem::observer_set<badem::transaction const &, std::shared_ptr<badem::vote>, std::shared_ptr<badem::transport::channel>> vote;
	badem::observer_set<badem::block_hash const &> active_stopped;
	badem::observer_set<badem::account const &, bool> account_balance;
	badem::observer_set<std::shared_ptr<badem::transport::channel>> endpoint;
	badem::observer_set<> disconnect;
	badem::observer_set<uint64_t> difficulty;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node_observers & node_observers, const std::string & name);
}
