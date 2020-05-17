#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/diagnosticsconfig.hpp>
#include <badem/lib/logger_mt.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/node/lmdb/lmdb_env.hpp>
#include <badem/node/lmdb/lmdb_iterator.hpp>
#include <badem/node/lmdb/lmdb_txn.hpp>
#include <badem/secure/blockstore_partial.hpp>
#include <badem/secure/common.hpp>
#include <badem/secure/versioning.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <thread>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace badem
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store_partial<MDB_val, mdb_store>
{
public:
	using block_store_partial::block_exists;
	using block_store_partial::unchecked_put;

	mdb_store (badem::logger_mt &, boost::filesystem::path const &, badem::txn_tracking_config const & txn_tracking_config_a = badem::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, size_t batch_size = 512, bool backup_before_upgrade = false);
	badem::write_transaction tx_begin_write (std::vector<badem::tables> const & tables_requiring_lock = {}, std::vector<badem::tables> const & tables_no_lock = {}) override;
	badem::read_transaction tx_begin_read () override;

	bool block_info_get (badem::transaction const &, badem::block_hash const &, badem::block_info &) const override;

	void version_put (badem::write_transaction const &, int) override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (badem::mdb_env &, boost::filesystem::path const &, badem::logger_mt &);

private:
	badem::logger_mt & logger;
	bool error{ false };

public:
	badem::mdb_env env;

	/**
	 * Maps head block to owning account
	 * badem::block_hash -> badem::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * badem::account -> badem::block_hash, badem::block_hash, badem::block_hash, badem::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * badem::account -> badem::block_hash, badem::block_hash, badem::block_hash, badem::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch. (Removed)
	 * badem::account -> badem::block_hash, badem::block_hash, badem::block_hash, badem::amount, uint64_t, uint64_t, badem::epoch
	 */
	MDB_dbi accounts{ 0 };

	/**
	 * Maps block hash to send block.
	 * badem::block_hash -> badem::send_block
	 */
	MDB_dbi send_blocks{ 0 };

	/**
	 * Maps block hash to receive block.
	 * badem::block_hash -> badem::receive_block
	 */
	MDB_dbi receive_blocks{ 0 };

	/**
	 * Maps block hash to open block.
	 * badem::block_hash -> badem::open_block
	 */
	MDB_dbi open_blocks{ 0 };

	/**
	 * Maps block hash to change block.
	 * badem::block_hash -> badem::change_block
	 */
	MDB_dbi change_blocks{ 0 };

	/**
	 * Maps block hash to v0 state block. (Removed)
	 * badem::block_hash -> badem::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block. (Removed)
	 * badem::block_hash -> badem::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps block hash to state block.
	 * badem::block_hash -> badem::state_block
	 */
	MDB_dbi state_blocks{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * badem::account, badem::block_hash -> badem::account, badem::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * badem::account, badem::block_hash -> badem::account, badem::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * badem::account, badem::block_hash -> badem::account, badem::amount, badem::epoch
	 */
	MDB_dbi pending{ 0 };

	/**
	 * Maps block hash to account and balance. (Removed)
	 * block_hash -> badem::account, badem::amount
	 */
	MDB_dbi blocks_info{ 0 };

	/**
	 * Representative weights. (Removed)
	 * badem::account -> badem::uint128_t
	 */
	MDB_dbi representation{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * badem::block_hash -> badem::unchecked_info
	 */
	MDB_dbi unchecked{ 0 };

	/**
	 * Highest vote observed for account.
	 * badem::account -> uint64_t
	 */
	MDB_dbi vote{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> badem::amount
	 */
	MDB_dbi online_weight{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * badem::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta{ 0 };

	/*
	 * Endpoints for peers
	 * badem::endpoint_key -> no_value
	*/
	MDB_dbi peers{ 0 };

	/*
	 * Confirmation height of an account
	 * badem::account -> uint64_t
	 */
	MDB_dbi confirmation_height{ 0 };

	bool exists (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a) const;

	int get (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a, badem::mdb_val & value_a) const;
	int put (badem::write_transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a, const badem::mdb_val & value_a) const;
	int del (badem::write_transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a) const;

	bool copy_db (boost::filesystem::path const & destination_file) override;

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a) const
	{
		return badem::store_iterator<Key, Value> (std::make_unique<badem::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a)));
	}

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key) const
	{
		return badem::store_iterator<Key, Value> (std::make_unique<badem::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	bool init_error () const override;

	size_t count (badem::transaction const &, MDB_dbi) const;

	// These are only use in the upgrade process.
	std::shared_ptr<badem::block> block_get_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const override;
	bool entry_has_sideband_v14 (size_t entry_size_a, badem::block_type type_a) const;
	size_t block_successor_offset_v14 (badem::transaction const & transaction_a, size_t entry_size_a, badem::block_type type_a) const;
	badem::block_hash block_successor_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const;
	badem::mdb_val block_raw_get_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a, bool * is_state_v1 = nullptr) const;
	boost::optional<badem::mdb_val> block_raw_get_by_type_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a, bool * is_state_v1) const;
	badem::account block_account_computed_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const;
	badem::account block_account_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const;
	badem::uint128_t block_balance_computed_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const;

private:
	bool do_upgrades (badem::write_transaction &, bool &, size_t);
	void upgrade_v1_to_v2 (badem::write_transaction const &);
	void upgrade_v2_to_v3 (badem::write_transaction const &);
	void upgrade_v3_to_v4 (badem::write_transaction const &);
	void upgrade_v4_to_v5 (badem::write_transaction const &);
	void upgrade_v5_to_v6 (badem::write_transaction const &);
	void upgrade_v6_to_v7 (badem::write_transaction const &);
	void upgrade_v7_to_v8 (badem::write_transaction const &);
	void upgrade_v8_to_v9 (badem::write_transaction const &);
	void upgrade_v10_to_v11 (badem::write_transaction const &);
	void upgrade_v11_to_v12 (badem::write_transaction const &);
	void upgrade_v12_to_v13 (badem::write_transaction &, size_t);
	void upgrade_v13_to_v14 (badem::write_transaction const &);
	void upgrade_v14_to_v15 (badem::write_transaction &);
	void open_databases (bool &, badem::transaction const &, unsigned);

	int drop (badem::write_transaction const & transaction_a, tables table_a) override;
	int clear (badem::write_transaction const & transaction_a, MDB_dbi handle_a);

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;

	MDB_dbi table_to_dbi (tables table_a) const;

	badem::mdb_txn_tracker mdb_txn_tracker;
	badem::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;

	size_t count (badem::transaction const & transaction_a, tables table_a) const override;

	bool vacuum_after_upgrade (boost::filesystem::path const & path_a, int lmdb_max_dbs);

	class upgrade_counters
	{
	public:
		upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
		bool are_equal () const;

		uint64_t before_v0;
		uint64_t before_v1;
		uint64_t after_v0{ 0 };
		uint64_t after_v1{ 0 };
	};
};

template <>
void * mdb_val::data () const;
template <>
size_t mdb_val::size () const;
template <>
mdb_val::db_val (size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();
}
