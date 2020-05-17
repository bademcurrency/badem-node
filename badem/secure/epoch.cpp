#include <badem/secure/epoch.hpp>

badem::link const & badem::epochs::link (badem::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool badem::epochs::is_epoch_link (badem::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; });
}

badem::public_key const & badem::epochs::signer (badem::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

badem::epoch badem::epochs::epoch (badem::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; }));
	assert (existing != epochs_m.end ());
	return existing->first;
}

void badem::epochs::add (badem::epoch epoch_a, badem::public_key const & signer_a, badem::link const & link_a)
{
	assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool badem::epochs::is_sequential (badem::epoch epoch_a, badem::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<badem::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<badem::epoch> (badem::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<badem::epoch> (new_epoch_a) == (head_epoch + 1));
}

std::underlying_type_t<badem::epoch> badem::normalized_epoch (badem::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<badem::epoch> (badem::epoch::epoch_0);
	auto end = std::underlying_type_t<badem::epoch> (epoch_a);
	assert (end >= start);
	return end - start;
}
