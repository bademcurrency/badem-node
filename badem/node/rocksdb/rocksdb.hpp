#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/logger_mt.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/node/rocksdb/rocksdb_iterator.hpp>
#include <badem/secure/blockstore_partial.hpp>
#include <badem/secure/common.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace badem
{
class logging_mt;
class rocksdb_config;
/**
 * rocksdb implementation of the block store
 */
class rocksdb_store : public block_store_partial<rocksdb::Slice, rocksdb_store>
{
public:
	rocksdb_store (badem::logger_mt &, boost::filesystem::path const &, badem::rocksdb_config const & = badem::rocksdb_config{}, bool open_read_only = false);
	~rocksdb_store ();
	badem::write_transaction tx_begin_write (std::vector<badem::tables> const & tables_requiring_lock = {}, std::vector<badem::tables> const & tables_no_lock = {}) override;
	badem::read_transaction tx_begin_read () override;

	bool block_info_get (badem::transaction const &, badem::block_hash const &, badem::block_info &) const override;
	size_t count (badem::transaction const & transaction_a, tables table_a) const override;
	void version_put (badem::write_transaction const &, int) override;

	bool exists (badem::transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a) const;
	int get (badem::transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a, badem::rocksdb_val & value_a) const;
	int put (badem::write_transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a, badem::rocksdb_val const & value_a);
	int del (badem::write_transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a);

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override
	{
		// Do nothing
	}

	std::shared_ptr<badem::block> block_get_v14 (badem::transaction const &, badem::block_hash const &, badem::block_sideband_v14 * = nullptr, bool * = nullptr) const override
	{
		// Should not be called as RocksDB has no such upgrade path
		release_assert (false);
		return nullptr;
	}

	bool copy_db (boost::filesystem::path const & destination) override;

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a) const
	{
		return badem::store_iterator<Key, Value> (std::make_unique<badem::rocksdb_iterator<Key, Value>> (db, transaction_a, table_to_column_family (table_a)));
	}

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key) const
	{
		return badem::store_iterator<Key, Value> (std::make_unique<badem::rocksdb_iterator<Key, Value>> (db, transaction_a, table_to_column_family (table_a), key));
	}

	bool init_error () const override;

private:
	bool error{ false };
	badem::logger_mt & logger;
	std::vector<rocksdb::ColumnFamilyHandle *> handles;
	// Optimistic transactions are used in write mode
	rocksdb::OptimisticTransactionDB * optimistic_db = nullptr;
	rocksdb::DB * db = nullptr;
	std::shared_ptr<rocksdb::TableFactory> table_factory;
	std::unordered_map<badem::tables, std::mutex> write_lock_mutexes;

	rocksdb::Transaction * tx (badem::transaction const & transaction_a) const;
	std::vector<badem::tables> all_tables () const;

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;
	int drop (badem::write_transaction const &, tables) override;

	rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a);
	uint64_t count (badem::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle) const;
	bool is_caching_counts (badem::tables table_a) const;

	int increment (badem::write_transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a, uint64_t amount_a);
	int decrement (badem::write_transaction const & transaction_a, tables table_a, badem::rocksdb_val const & key_a, uint64_t amount_a);
	rocksdb::ColumnFamilyOptions get_cf_options () const;
	void construct_column_family_mutexes ();
	rocksdb::Options get_db_options () const;
	rocksdb::BlockBasedTableOptions get_table_options () const;
	badem::rocksdb_config rocksdb_config;
};
}
