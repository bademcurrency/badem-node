#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

namespace badem
{
class node;
class transaction;

/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (badem::node &, badem::uint128_t);

	/** Add voting account \p rep_account to the set of online representatives */
	void observe (badem::account const & rep_account);
	/** Called periodically to sample online weight */
	void sample ();
	/** Returns the trended online stake, but never less than configured minimum */
	badem::uint128_t online_stake () const;
	/** List of online representatives */
	std::vector<badem::account> list ();

private:
	badem::uint128_t trend (badem::transaction &);
	mutable std::mutex mutex;
	badem::node & node;
	std::unordered_set<badem::account> reps;
	badem::uint128_t online;
	badem::uint128_t minimum;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);
}
