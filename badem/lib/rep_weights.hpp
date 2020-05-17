#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace badem
{
class block_store;
class transaction;

class rep_weights
{
public:
	void representation_add (badem::account const & source_a, badem::uint128_t const & amount_a);
	badem::uint128_t representation_get (badem::account const & account_a);
	void representation_put (badem::account const & account_a, badem::uint128_union const & representation_a);
	std::unordered_map<badem::account, badem::uint128_t> get_rep_amounts ();

private:
	std::mutex mutex;
	std::unordered_map<badem::account, badem::uint128_t> rep_amounts;
	void put (badem::account const & account_a, badem::uint128_union const & representation_a);
	badem::uint128_t get (badem::account const & account_a);

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_weights &, const std::string &);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_weights &, const std::string &);
}
