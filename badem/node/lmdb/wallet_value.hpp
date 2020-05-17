#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/secure/blockstore.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace badem
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (badem::db_val<MDB_val> const &);
	wallet_value (badem::uint256_union const &, uint64_t);
	badem::db_val<MDB_val> val () const;
	badem::uint256_union key;
	uint64_t work;
};
}
