#include <badem/lib/rep_weights.hpp>
#include <badem/secure/blockstore.hpp>

void badem::rep_weights::representation_add (badem::account const & source_rep, badem::uint128_t const & amount_a)
{
	badem::lock_guard<std::mutex> guard (mutex);
	auto source_previous (get (source_rep));
	put (source_rep, source_previous + amount_a);
}

void badem::rep_weights::representation_put (badem::account const & account_a, badem::uint128_union const & representation_a)
{
	badem::lock_guard<std::mutex> guard (mutex);
	put (account_a, representation_a);
}

badem::uint128_t badem::rep_weights::representation_get (badem::account const & account_a)
{
	badem::lock_guard<std::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<badem::account, badem::uint128_t> badem::rep_weights::get_rep_amounts ()
{
	badem::lock_guard<std::mutex> guard (mutex);
	return rep_amounts;
}

void badem::rep_weights::put (badem::account const & account_a, badem::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	auto amount = representation_a.number ();
	if (it != rep_amounts.end ())
	{
		it->second = amount;
	}
	else
	{
		rep_amounts.emplace (account_a, amount);
	}
}

badem::uint128_t badem::rep_weights::get (badem::account const & account_a)
{
	auto it = rep_amounts.find (account_a);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return badem::uint128_t{ 0 };
	}
}

std::unique_ptr<badem::seq_con_info_component> badem::collect_seq_con_info (badem::rep_weights & rep_weights, const std::string & name)
{
	size_t rep_amounts_count = 0;

	{
		badem::lock_guard<std::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<badem::seq_con_info_composite> (name);
	composite->add_component (std::make_unique<badem::seq_con_info_leaf> (seq_con_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
