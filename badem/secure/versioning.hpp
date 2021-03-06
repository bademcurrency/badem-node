#pragma once

#include <badem/lib/blocks.hpp>
#include <badem/secure/common.hpp>
#include <badem/secure/utility.hpp>

struct MDB_val;

namespace badem
{
class account_info_v1 final
{
public:
	account_info_v1 () = default;
	explicit account_info_v1 (MDB_val const &);
	account_info_v1 (badem::block_hash const &, badem::block_hash const &, badem::amount const &, uint64_t);
	badem::block_hash head{ 0 };
	badem::block_hash rep_block{ 0 };
	badem::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class pending_info_v3 final
{
public:
	pending_info_v3 () = default;
	explicit pending_info_v3 (MDB_val const &);
	pending_info_v3 (badem::account const &, badem::amount const &, badem::account const &);
	badem::account source{ 0 };
	badem::amount amount{ 0 };
	badem::account destination{ 0 };
};
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (badem::account const &, badem::amount const &, badem::epoch);
	size_t db_size () const;
	bool deserialize (badem::stream &);
	bool operator== (badem::pending_info_v14 const &) const;
	badem::account source{ 0 };
	badem::amount amount{ 0 };
	badem::epoch epoch{ badem::epoch::epoch_0 };
};
class account_info_v5 final
{
public:
	account_info_v5 () = default;
	explicit account_info_v5 (MDB_val const &);
	account_info_v5 (badem::block_hash const &, badem::block_hash const &, badem::block_hash const &, badem::amount const &, uint64_t);
	badem::block_hash head{ 0 };
	badem::block_hash rep_block{ 0 };
	badem::block_hash open_block{ 0 };
	badem::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class account_info_v13 final
{
public:
	account_info_v13 () = default;
	account_info_v13 (badem::block_hash const &, badem::block_hash const &, badem::block_hash const &, badem::amount const &, uint64_t, uint64_t, badem::epoch);
	size_t db_size () const;
	badem::block_hash head{ 0 };
	badem::block_hash rep_block{ 0 };
	badem::block_hash open_block{ 0 };
	badem::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	badem::epoch epoch{ badem::epoch::epoch_0 };
};
class account_info_v14 final
{
public:
	account_info_v14 () = default;
	account_info_v14 (badem::block_hash const &, badem::block_hash const &, badem::block_hash const &, badem::amount const &, uint64_t, uint64_t, uint64_t, badem::epoch);
	size_t db_size () const;
	badem::block_hash head{ 0 };
	badem::block_hash rep_block{ 0 };
	badem::block_hash open_block{ 0 };
	badem::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	badem::epoch epoch{ badem::epoch::epoch_0 };
};
class block_sideband_v14 final
{
public:
	block_sideband_v14 () = default;
	block_sideband_v14 (badem::block_type, badem::account const &, badem::block_hash const &, badem::amount const &, uint64_t, uint64_t);
	void serialize (badem::stream &) const;
	bool deserialize (badem::stream &);
	static size_t size (badem::block_type);
	badem::block_type type{ badem::block_type::invalid };
	badem::block_hash successor{ 0 };
	badem::account account{ 0 };
	badem::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<badem::state_block> state_block;
	badem::block_sideband_v14 sideband;
};
}
