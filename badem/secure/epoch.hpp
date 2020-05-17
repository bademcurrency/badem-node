#pragma once

#include <badem/lib/numbers.hpp>

#include <type_traits>
#include <unordered_map>

namespace badem
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_2 = 4,
	max = epoch_2
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<badem::epoch> normalized_epoch (badem::epoch epoch_a);
}
namespace std
{
template <>
struct hash<::badem::epoch>
{
	std::size_t operator() (::badem::epoch const & epoch_a) const
	{
		std::hash<std::underlying_type_t<::badem::epoch>> hash;
		return hash (static_cast<std::underlying_type_t<::badem::epoch>> (epoch_a));
	}
};
}
namespace badem
{
class epoch_info
{
public:
	badem::public_key signer;
	badem::link link;
};
class epochs
{
public:
	bool is_epoch_link (badem::link const & link_a) const;
	badem::link const & link (badem::epoch epoch_a) const;
	badem::public_key const & signer (badem::epoch epoch_a) const;
	badem::epoch epoch (badem::link const & link_a) const;
	void add (badem::epoch epoch_a, badem::public_key const & signer_a, badem::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (badem::epoch epoch_a, badem::epoch new_epoch_a);

private:
	std::unordered_map<badem::epoch, badem::epoch_info> epochs_m;
};
}
