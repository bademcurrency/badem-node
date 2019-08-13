#include <badem/node/lmdb/wallet_value.hpp>

badem::wallet_value::wallet_value (badem::db_val<MDB_val> const & val_a)
{
	assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

badem::wallet_value::wallet_value (badem::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

badem::db_val<MDB_val> badem::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return badem::db_val<MDB_val> (sizeof (*this), const_cast<badem::wallet_value *> (this));
}
