#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/utility.hpp>
#include <badem/node/common.hpp>
#include <badem/node/lmdb/lmdb.hpp>
#include <badem/node/lmdb/lmdb_iterator.hpp>
#include <badem/node/lmdb/wallet_value.hpp>
#include <badem/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <queue>

namespace badem
{
template <>
void * mdb_val::data () const
{
	return value.mv_data;
}

template <>
size_t mdb_val::size () const
{
	return value.mv_size;
}

template <>
mdb_val::db_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

template <>
void mdb_val::convert_buffer_to_value ()
{
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}
}

badem::mdb_store::mdb_store (badem::logger_mt & logger_a, boost::filesystem::path const & path_a, badem::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, size_t const batch_size, bool backup_before_upgrade) :
logger (logger_a),
env (error, path_a, lmdb_max_dbs, true),
mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error)
	{
		auto is_fully_upgraded (false);
		{
			auto transaction (tx_begin_read ());
			auto err = mdb_dbi_open (env.tx (transaction), "meta", 0, &meta);
			if (err == MDB_SUCCESS)
			{
				is_fully_upgraded = (version_get (transaction) == version);
				mdb_dbi_close (env, meta);
			}
		}

		// Only open a write lock when upgrades are needed. This is because CLI commands
		// open inactive nodes which can otherwise be locked here if there is a long write
		// (can be a few minutes with the --fast_bootstrap flag for instance)
		if (!is_fully_upgraded)
		{
			if (backup_before_upgrade)
			{
				create_backup_file (env, path_a, logger_a);
			}
			auto needs_vacuuming = false;
			{
				auto transaction (tx_begin_write ());
				open_databases (error, transaction, MDB_CREATE);
				if (!error)
				{
					error |= do_upgrades (transaction, needs_vacuuming, batch_size);
				}
			}

			if (needs_vacuuming)
			{
				auto vacuum_success = vacuum_after_upgrade (path_a, lmdb_max_dbs);
				logger.always_log (vacuum_success ? "Vacuum succeeded." : "Failed to vacuum. (Optional) Ensure enough disk space is available for a copy of the database and try to vacuum after shutting down the node");
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error, transaction, 0);
		}
	}
}

bool badem::mdb_store::vacuum_after_upgrade (boost::filesystem::path const & path_a, int lmdb_max_dbs)
{
	// Vacuum the database. This is not a required step and may actually fail if there isn't enough storage space.
	auto vacuum_path = path_a.parent_path () / "vacuumed.ldb";

	auto vacuum_success = copy_db (vacuum_path);
	if (vacuum_success)
	{
		// Need to close the database to release the file handle
		mdb_env_close (env.environment);
		env.environment = nullptr;

		// Replace the ledger file with the vacuumed one
		boost::filesystem::rename (vacuum_path, path_a);

		// Set up the environment again
		env.init (error, path_a, lmdb_max_dbs, true);
		if (!error)
		{
			auto transaction (tx_begin_read ());
			open_databases (error, transaction, 0);
		}
	}
	else
	{
		// The vacuum file can be in an inconsistent state if there wasn't enough space to create it
		boost::filesystem::remove (vacuum_path);
	}
	return vacuum_success;
}

void badem::mdb_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

badem::write_transaction badem::mdb_store::tx_begin_write (std::vector<badem::tables> const &, std::vector<badem::tables> const &)
{
	return env.tx_begin_write (create_txn_callbacks ());
}

badem::read_transaction badem::mdb_store::tx_begin_read ()
{
	return env.tx_begin_read (create_txn_callbacks ());
}

badem::mdb_txn_callbacks badem::mdb_store::create_txn_callbacks ()
{
	badem::mdb_txn_callbacks mdb_txn_callbacks;
	if (txn_tracking_enabled)
	{
		// clang-format off
		mdb_txn_callbacks.txn_start = ([&mdb_txn_tracker = mdb_txn_tracker](const badem::transaction_impl * transaction_impl) {
			mdb_txn_tracker.add (transaction_impl);
		});
		mdb_txn_callbacks.txn_end = ([&mdb_txn_tracker = mdb_txn_tracker](const badem::transaction_impl * transaction_impl) {
			mdb_txn_tracker.erase (transaction_impl);
		});
		// clang-format on
	}
	return mdb_txn_callbacks;
}

void badem::mdb_store::open_databases (bool & error_a, badem::transaction const & transaction_a, unsigned flags)
{
	error_a |= mdb_dbi_open (env.tx (transaction_a), "frontiers", flags, &frontiers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "send", flags, &send_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "receive", flags, &receive_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "open", flags, &open_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "change", flags, &change_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "unchecked", flags, &unchecked) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "vote", flags, &vote) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "online_weight", flags, &online_weight) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "meta", flags, &meta) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "peers", flags, &peers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "confirmation_height", flags, &confirmation_height) != 0;
	if (!full_sideband (transaction_a))
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "blocks_info", flags, &blocks_info) != 0;
	}
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &accounts_v0) != 0;
	accounts = accounts_v0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_v0) != 0;
	pending = pending_v0;

	if (version_get (transaction_a) < 15)
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "state", flags, &state_blocks_v0) != 0;
		state_blocks = state_blocks_v0;
		error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts_v1", flags, &accounts_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction_a), "pending_v1", flags, &pending_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction_a), "state_v1", flags, &state_blocks_v1) != 0;
	}
	else
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "state_blocks", flags, &state_blocks) != 0;
		state_blocks_v0 = state_blocks;
	}
}

bool badem::mdb_store::do_upgrades (badem::write_transaction & transaction_a, bool & needs_vacuuming, size_t batch_size_a)
{
	auto error (false);
	auto version_l = version_get (transaction_a);
	switch (version_l)
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			upgrade_v11_to_v12 (transaction_a);
		case 12:
			upgrade_v12_to_v13 (transaction_a, batch_size_a);
		case 13:
			upgrade_v13_to_v14 (transaction_a);
		case 14:
			upgrade_v14_to_v15 (transaction_a);
			needs_vacuuming = true;
		case 15:
			break;
		default:
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
			error = true;
			break;
	}
	return error;
}

void badem::mdb_store::upgrade_v1_to_v2 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	badem::account account (1);
	while (!account.is_zero ())
	{
		badem::mdb_iterator<badem::account, badem::account_info_v1> i (transaction_a, accounts_v0, badem::mdb_val (account));
		std::cerr << std::hex;
		if (i != badem::mdb_iterator<badem::account, badem::account_info_v1>{})
		{
			account = badem::account (i->first);
			badem::account_info_v1 v1 (i->second);
			badem::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, badem::mdb_val (account), badem::mdb_val (sizeof (v2), &v2), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void badem::mdb_store::upgrade_v2_to_v3 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> ()); *i != *n; ++(*i))
	{
		badem::account account_l ((*i)->first);
		badem::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<badem::mdb_iterator<badem::account, badem::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, badem::mdb_val (account_l), badem::mdb_val (sizeof (info), &info), MDB_CURRENT);
	}
}

void badem::mdb_store::upgrade_v3_to_v4 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<badem::pending_key, badem::pending_info_v14>> items;
	for (auto i (badem::store_iterator<badem::block_hash, badem::pending_info_v3> (std::make_unique<badem::mdb_iterator<badem::block_hash, badem::pending_info_v3>> (transaction_a, pending_v0))), n (badem::store_iterator<badem::block_hash, badem::pending_info_v3> (nullptr)); i != n; ++i)
	{
		badem::block_hash const & hash (i->first);
		badem::pending_info_v3 const & info (i->second);
		items.push (std::make_pair (badem::pending_key (info.destination, hash), badem::pending_info_v14 (info.source, info.amount, badem::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		auto status (mdb_put (env.tx (transaction_a), pending, badem::mdb_val (items.front ().first), badem::mdb_val (items.front ().second), 0));
		assert (success (status));
		items.pop ();
	}
}

void badem::mdb_store::upgrade_v4_to_v5 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (badem::store_iterator<badem::account, badem::account_info_v5> (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> (transaction_a, accounts_v0))), n (badem::store_iterator<badem::account, badem::account_info_v5> (nullptr)); i != n; ++i)
	{
		badem::account_info_v5 const & info (i->second);
		badem::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				std::vector<uint8_t> vector;
				{
					badem::vectorstream stream (vector);
					block->serialize (stream);
					badem::write (stream, successor.bytes);
				}
				block_raw_put (transaction_a, vector, block->type (), hash);
				if (!block->previous ().is_zero ())
				{
					badem::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					assert (value.size () != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - badem::block_sideband::size (type));
					block_raw_put (transaction_a, data, type, block->previous ());
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void badem::mdb_store::upgrade_v5_to_v6 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<badem::account, badem::account_info_v13>> headers;
	for (auto i (badem::store_iterator<badem::account, badem::account_info_v5> (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> (transaction_a, accounts_v0))), n (badem::store_iterator<badem::account, badem::account_info_v5> (nullptr)); i != n; ++i)
	{
		badem::account const & account (i->first);
		badem::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		headers.emplace_back (account, badem::account_info_v13{ info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, badem::epoch::epoch_0 });
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		auto status (mdb_put (env.tx (transaction_a), accounts_v0, badem::mdb_val (i->first), badem::mdb_val (i->second), 0));
		release_assert (status == 0);
	}
}

void badem::mdb_store::upgrade_v6_to_v7 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void badem::mdb_store::upgrade_v7_to_v8 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void badem::mdb_store::upgrade_v8_to_v9 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	badem::genesis genesis;
	std::shared_ptr<badem::block> block (std::move (genesis.open));
	badem::keypair junk;
	for (badem::mdb_iterator<badem::account, uint64_t> i (transaction_a, sequence), n (badem::mdb_iterator<badem::account, uint64_t>{}); i != n; ++i)
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (badem::try_read (stream, sequence));
		(void)error;
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		badem::vote dummy (badem::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			badem::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, badem::mdb_val (i->first), badem::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void badem::mdb_store::upgrade_v10_to_v11 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void badem::mdb_store::upgrade_v11_to_v12 (badem::write_transaction const & transaction_a)
{
	version_put (transaction_a, 12);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked);
	MDB_dbi checksum;
	mdb_dbi_open (env.tx (transaction_a), "checksum", MDB_CREATE, &checksum);
	mdb_drop (env.tx (transaction_a), checksum, 1);
}

void badem::mdb_store::upgrade_v12_to_v13 (badem::write_transaction & transaction_a, size_t const batch_size)
{
	size_t cost (0);
	badem::account account (0);
	auto const & not_an_account (network_params.random.not_an_account);
	while (account != not_an_account)
	{
		badem::account first (0);
		badem::account_info_v13 second;
		{
			badem::mdb_merge_iterator<badem::account, badem::account_info_v13> current (transaction_a, accounts_v0, accounts_v1, badem::mdb_val (account));
			badem::mdb_merge_iterator<badem::account, badem::account_info_v13> end{};
			if (current != end)
			{
				first = badem::account (current->first);
				second = badem::account_info_v13 (current->second);
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			badem::block_sideband_v14 sideband;
			while (!hash.is_zero ())
			{
				if (cost >= batch_size)
				{
					logger.always_log (boost::str (boost::format ("Upgrading sideband information for account %1%... height %2%") % first.to_account ().substr (0, 24) % std::to_string (height)));
					transaction_a.commit ();
					std::this_thread::yield ();
					transaction_a.renew ();
					cost = 0;
				}

				bool is_state_block_v1 = false;
				auto block = block_get_v14 (transaction_a, hash, &sideband, &is_state_block_v1);

				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;

					std::vector<uint8_t> vector;
					{
						badem::vectorstream stream (vector);
						block->serialize (stream);
						sideband.serialize (stream);
					}

					badem::mdb_val value{ vector.size (), (void *)vector.data () };
					MDB_dbi database = is_state_block_v1 ? state_blocks_v1 : table_to_dbi (block_database (sideband.type));

					auto status = mdb_put (env.tx (transaction_a), database, badem::mdb_val (hash), value, 0);
					release_assert (success (status));

					badem::block_predecessor_set<MDB_val, badem::mdb_store> predecessor (transaction_a, *this);
					block->visit (predecessor);
					assert (block->previous ().is_zero () || block_successor (transaction_a, block->previous ()) == hash);
					cost += 16;
				}
				else
				{
					cost += 1;
				}
				hash = sideband.successor;
				++height;
			}
			account = first.number () + 1;
		}
		else
		{
			account = not_an_account;
		}
	}
	if (account == not_an_account)
	{
		logger.always_log ("Completed sideband upgrade");
		version_put (transaction_a, 13);
	}
}

void badem::mdb_store::upgrade_v13_to_v14 (badem::write_transaction const & transaction_a)
{
	// Upgrade all accounts to have a confirmation of 0 (except genesis which should have 1)
	version_put (transaction_a, 14);
	badem::mdb_merge_iterator<badem::account, badem::account_info_v13> i (transaction_a, accounts_v0, accounts_v1);
	badem::mdb_merge_iterator<badem::account, badem::account_info_v13> n{};
	std::vector<std::pair<badem::account, badem::account_info_v14>> account_infos;
	account_infos.reserve (count (transaction_a, accounts_v0) + count (transaction_a, accounts_v1));
	for (; i != n; ++i)
	{
		badem::account account (i->first);
		badem::account_info_v13 account_info_v13 (i->second);

		uint64_t confirmation_height = 0;
		if (account == network_params.ledger.genesis_account)
		{
			confirmation_height = 1;
		}
		account_infos.emplace_back (account, badem::account_info_v14{ account_info_v13.head, account_info_v13.rep_block, account_info_v13.open_block, account_info_v13.balance, account_info_v13.modified, account_info_v13.block_count, confirmation_height, i.from_first_database ? badem::epoch::epoch_0 : badem::epoch::epoch_1 });
	}

	for (auto const & account_info : account_infos)
	{
		auto status1 (mdb_put (env.tx (transaction_a), account_info.second.epoch == badem::epoch::epoch_0 ? accounts_v0 : accounts_v1, badem::mdb_val (account_info.first), badem::mdb_val (account_info.second), 0));
		release_assert (status1 == 0);
	}

	logger.always_log ("Completed confirmation height upgrade");

	badem::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, badem::mdb_val (node_id_mdb_key), nullptr));
	release_assert (!error || error == MDB_NOTFOUND);
}

void badem::mdb_store::upgrade_v14_to_v15 (badem::write_transaction & transaction_a)
{
	logger.always_log ("Preparing v14 to v15 upgrade...");

	std::vector<std::pair<badem::account, badem::account_info>> account_infos;
	upgrade_counters account_counters (count (transaction_a, accounts_v0), count (transaction_a, accounts_v1));
	account_infos.reserve (account_counters.before_v0 + account_counters.before_v1);

	badem::mdb_merge_iterator<badem::account, badem::account_info_v14> i_account (transaction_a, accounts_v0, accounts_v1);
	badem::mdb_merge_iterator<badem::account, badem::account_info_v14> n_account{};
	for (; i_account != n_account; ++i_account)
	{
		badem::account account (i_account->first);
		badem::account_info_v14 account_info_v14 (i_account->second);

		// Upgrade rep block to representative account
		auto rep_block = block_get_v14 (transaction_a, account_info_v14.rep_block);
		release_assert (rep_block != nullptr);
		account_infos.emplace_back (account, badem::account_info{ account_info_v14.head, rep_block->representative (), account_info_v14.open_block, account_info_v14.balance, account_info_v14.modified, account_info_v14.block_count, i_account.from_first_database ? badem::epoch::epoch_0 : badem::epoch::epoch_1 });
		// Move confirmation height from account_info database to its own table
		confirmation_height_put (transaction_a, account, account_info_v14.confirmation_height);
		i_account.from_first_database ? ++account_counters.after_v0 : ++account_counters.after_v1;
	}

	logger.always_log ("Finished extracting confirmation height to its own database");

	assert (account_counters.are_equal ());
	// No longer need accounts_v1, keep v0 but clear it
	mdb_drop (env.tx (transaction_a), accounts_v1, 1);
	mdb_drop (env.tx (transaction_a), accounts_v0, 0);

	for (auto const & account_account_info_pair : account_infos)
	{
		auto const & account_info (account_account_info_pair.second);
		mdb_put (env.tx (transaction_a), accounts, badem::mdb_val (account_account_info_pair.first), badem::mdb_val (account_info), MDB_APPEND);
	}

	logger.always_log ("Epoch merge upgrade: Finished accounts, now doing state blocks");

	account_infos.clear ();

	// Have to create a new database as we are iterating over the existing ones and want to use MDB_APPEND for quick insertion
	MDB_dbi state_blocks_new;
	mdb_dbi_open (env.tx (transaction_a), "state_blocks", MDB_CREATE, &state_blocks_new);

	upgrade_counters state_counters (count (transaction_a, state_blocks_v0), count (transaction_a, state_blocks_v1));

	badem::mdb_merge_iterator<badem::block_hash, badem::state_block_w_sideband_v14> i_state (transaction_a, state_blocks_v0, state_blocks_v1);
	badem::mdb_merge_iterator<badem::block_hash, badem::state_block_w_sideband_v14> n_state{};
	auto num = 0u;
	for (; i_state != n_state; ++i_state, ++num)
	{
		badem::block_hash hash (i_state->first);
		badem::state_block_w_sideband_v14 state_block_w_sideband_v14 (i_state->second);
		auto & sideband_v14 = state_block_w_sideband_v14.sideband;

		badem::block_sideband sideband{ sideband_v14.type, sideband_v14.account, sideband_v14.successor, sideband_v14.balance, sideband_v14.height, sideband_v14.timestamp, i_state.from_first_database ? badem::epoch::epoch_0 : badem::epoch::epoch_1 };

		// Write these out
		std::vector<uint8_t> data;
		{
			badem::vectorstream stream (data);
			state_block_w_sideband_v14.state_block->serialize (stream);
			sideband.serialize (stream);
		}

		badem::mdb_val value{ data.size (), (void *)data.data () };
		auto s = mdb_put (env.tx (transaction_a), state_blocks_new, badem::mdb_val (hash), value, MDB_APPEND);
		release_assert (success (s));

		// Every so often output to the log to indicate progress
		constexpr auto output_cutoff = 1000000;
		if (num % output_cutoff == 0 && num != 0)
		{
			logger.always_log (boost::str (boost::format ("Database epoch merge upgrade %1% million state blocks upgraded") % (num / output_cutoff)));
		}
		i_state.from_first_database ? ++state_counters.after_v0 : ++state_counters.after_v1;
	}

	assert (state_counters.are_equal ());
	logger.always_log ("Epoch merge upgrade: Finished state blocks, now doing pending blocks");

	state_blocks = state_blocks_new;

	// No longer need states v0/v1 databases
	mdb_drop (env.tx (transaction_a), state_blocks_v1, 1);
	mdb_drop (env.tx (transaction_a), state_blocks_v0, 1);

	state_blocks_v0 = state_blocks;

	upgrade_counters pending_counters (count (transaction_a, pending_v0), count (transaction_a, pending_v1));
	std::vector<std::pair<badem::pending_key, badem::pending_info>> pending_infos;
	pending_infos.reserve (pending_counters.before_v0 + pending_counters.before_v1);

	badem::mdb_merge_iterator<badem::pending_key, badem::pending_info_v14> i_pending (transaction_a, pending_v0, pending_v1);
	badem::mdb_merge_iterator<badem::pending_key, badem::pending_info_v14> n_pending{};
	for (; i_pending != n_pending; ++i_pending)
	{
		badem::pending_info_v14 info (i_pending->second);
		pending_infos.emplace_back (badem::pending_key (i_pending->first), badem::pending_info{ info.source, info.amount, i_pending.from_first_database ? badem::epoch::epoch_0 : badem::epoch::epoch_1 });
		i_pending.from_first_database ? ++pending_counters.after_v0 : ++pending_counters.after_v1;
	}

	assert (pending_counters.are_equal ());

	// No longer need the pending v1 table
	mdb_drop (env.tx (transaction_a), pending_v1, 1);
	mdb_drop (env.tx (transaction_a), pending_v0, 0);

	for (auto const & pending_key_pending_info_pair : pending_infos)
	{
		mdb_put (env.tx (transaction_a), pending, badem::mdb_val (pending_key_pending_info_pair.first), badem::mdb_val (pending_key_pending_info_pair.second), MDB_APPEND);
	}

	// Representation table is no longer used
	if (representation != 0)
	{
		auto status (mdb_drop (env.tx (transaction_a), representation, 1));
		release_assert (status == MDB_SUCCESS);
		representation = 0;
	}
	version_put (transaction_a, 15);
	logger.always_log ("Finished epoch merge upgrade. Preparing vacuum...");
}

/** Takes a filepath, appends '_backup_<timestamp>' to the end (but before any extension) and saves that file in the same directory */
void badem::mdb_store::create_backup_file (badem::mdb_env & env_a, boost::filesystem::path const & filepath_a, badem::logger_mt & logger_a)
{
	auto extension = filepath_a.extension ();
	auto filename_without_extension = filepath_a.filename ().replace_extension ("");
	auto orig_filepath = filepath_a;
	auto & backup_path = orig_filepath.remove_filename ();
	auto backup_filename = filename_without_extension;
	backup_filename += "_backup_";
	backup_filename += std::to_string (std::chrono::system_clock::now ().time_since_epoch ().count ());
	backup_filename += extension;
	auto backup_filepath = backup_path / backup_filename;
	auto start_message (boost::str (boost::format ("Performing %1% backup before database upgrade...") % filepath_a.filename ()));
	logger_a.always_log (start_message);
	std::cout << start_message << std::endl;
	auto error (mdb_env_copy (env_a, backup_filepath.string ().c_str ()));
	if (error)
	{
		auto error_message (boost::str (boost::format ("%1% backup failed") % filepath_a.filename ()));
		logger_a.always_log (error_message);
		std::cerr << error_message << std::endl;
		std::exit (1);
	}
	else
	{
		auto success_message (boost::str (boost::format ("Backup created: %1%") % backup_filename));
		logger_a.always_log (success_message);
		std::cout << success_message << std::endl;
	}
}

void badem::mdb_store::version_put (badem::write_transaction const & transaction_a, int version_a)
{
	badem::uint256_union version_key (1);
	badem::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, badem::mdb_val (version_key), badem::mdb_val (version_value), 0));
	release_assert (status == 0);
	if (blocks_info == 0 && !full_sideband (transaction_a))
	{
		auto status (mdb_dbi_open (env.tx (transaction_a), "blocks_info", MDB_CREATE, &blocks_info));
		release_assert (status == MDB_SUCCESS);
	}
	if (blocks_info != 0 && full_sideband (transaction_a))
	{
		auto status (mdb_drop (env.tx (transaction_a), blocks_info, 1));
		release_assert (status == MDB_SUCCESS);
		blocks_info = 0;
	}
}

bool badem::mdb_store::block_info_get (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_info & block_info_a) const
{
	assert (!full_sideband (transaction_a));
	badem::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, badem::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (badem::try_read (stream, block_info_a.account));
		(void)error1;
		assert (!error1);
		auto error2 (badem::try_read (stream, block_info_a.balance));
		(void)error2;
		assert (!error2);
	}
	return result;
}

bool badem::mdb_store::exists (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a) const
{
	badem::mdb_val junk;
	auto status = get (transaction_a, table_a, key_a, junk);
	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	return (status == MDB_SUCCESS);
}

int badem::mdb_store::get (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a, badem::mdb_val & value_a) const
{
	return mdb_get (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a);
}

int badem::mdb_store::put (badem::write_transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a, const badem::mdb_val & value_a) const
{
	return (mdb_put (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a, 0));
}

int badem::mdb_store::del (badem::write_transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a) const
{
	return (mdb_del (env.tx (transaction_a), table_to_dbi (table_a), key_a, nullptr));
}

int badem::mdb_store::drop (badem::write_transaction const & transaction_a, tables table_a)
{
	return clear (transaction_a, table_to_dbi (table_a));
}

int badem::mdb_store::clear (badem::write_transaction const & transaction_a, MDB_dbi handle_a)
{
	return mdb_drop (env.tx (transaction_a), handle_a, 0);
}

size_t badem::mdb_store::count (badem::transaction const & transaction_a, tables table_a) const
{
	return count (transaction_a, table_to_dbi (table_a));
}

size_t badem::mdb_store::count (badem::transaction const & transaction_a, MDB_dbi db_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), db_a, &stats));
	release_assert (status == 0);
	return (stats.ms_entries);
}

MDB_dbi badem::mdb_store::table_to_dbi (tables table_a) const
{
	switch (table_a)
	{
		case tables::frontiers:
			return frontiers;
		case tables::accounts:
			return accounts;
		case tables::send_blocks:
			return send_blocks;
		case tables::receive_blocks:
			return receive_blocks;
		case tables::open_blocks:
			return open_blocks;
		case tables::change_blocks:
			return change_blocks;
		case tables::state_blocks:
			return state_blocks;
		case tables::pending:
			return pending;
		case tables::blocks_info:
			return blocks_info;
		case tables::unchecked:
			return unchecked;
		case tables::vote:
			return vote;
		case tables::online_weight:
			return online_weight;
		case tables::meta:
			return meta;
		case tables::peers:
			return peers;
		case tables::confirmation_height:
			return confirmation_height;
		default:
			release_assert (false);
			return peers;
	}
}

bool badem::mdb_store::not_found (int status) const
{
	return (status_code_not_found () == status);
}

bool badem::mdb_store::success (int status) const
{
	return (MDB_SUCCESS == status);
}

int badem::mdb_store::status_code_not_found () const
{
	return MDB_NOTFOUND;
}

bool badem::mdb_store::copy_db (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

bool badem::mdb_store::init_error () const
{
	return error;
}

// All the v14 functions below are only needed during upgrades
bool badem::mdb_store::entry_has_sideband_v14 (size_t entry_size_a, badem::block_type type_a) const
{
	return (entry_size_a == badem::block::size (type_a) + badem::block_sideband_v14::size (type_a));
}

size_t badem::mdb_store::block_successor_offset_v14 (badem::transaction const & transaction_a, size_t entry_size_a, badem::block_type type_a) const
{
	size_t result;
	if (full_sideband (transaction_a) || entry_has_sideband_v14 (entry_size_a, type_a))
	{
		result = entry_size_a - badem::block_sideband_v14::size (type_a);
	}
	else
	{
		// Read old successor-only sideband
		assert (entry_size_a == badem::block::size (type_a) + sizeof (badem::uint256_union));
		result = entry_size_a - sizeof (badem::uint256_union);
	}
	return result;
}

badem::block_hash badem::mdb_store::block_successor_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	badem::block_type type;
	auto value (block_raw_get_v14 (transaction_a, hash_a, type));
	badem::block_hash result;
	if (value.size () != 0)
	{
		assert (value.size () >= result.bytes.size ());
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset_v14 (transaction_a, value.size (), type), result.bytes.size ());
		auto error (badem::try_read (stream, result.bytes));
		(void)error;
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

badem::mdb_val badem::mdb_store::block_raw_get_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a, bool * is_state_v1) const
{
	badem::mdb_val result;
	// Table lookups are ordered by match probability
	badem::block_type block_types[]{ badem::block_type::state, badem::block_type::send, badem::block_type::receive, badem::block_type::open, badem::block_type::change };
	for (auto current_type : block_types)
	{
		auto db_val (block_raw_get_by_type_v14 (transaction_a, hash_a, current_type, is_state_v1));
		if (db_val.is_initialized ())
		{
			type_a = current_type;
			result = db_val.get ();
			break;
		}
	}

	return result;
}

boost::optional<badem::mdb_val> badem::mdb_store::block_raw_get_by_type_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a, bool * is_state_v1) const
{
	badem::mdb_val value;
	badem::mdb_val hash (hash_a);
	int status = status_code_not_found ();
	switch (type_a)
	{
		case badem::block_type::send:
		{
			status = mdb_get (env.tx (transaction_a), send_blocks, hash, value);
			break;
		}
		case badem::block_type::receive:
		{
			status = mdb_get (env.tx (transaction_a), receive_blocks, hash, value);
			break;
		}
		case badem::block_type::open:
		{
			status = mdb_get (env.tx (transaction_a), open_blocks, hash, value);
			break;
		}
		case badem::block_type::change:
		{
			status = mdb_get (env.tx (transaction_a), change_blocks, hash, value);
			break;
		}
		case badem::block_type::state:
		{
			status = mdb_get (env.tx (transaction_a), state_blocks_v1, hash, value);
			if (is_state_v1 != nullptr)
			{
				*is_state_v1 = success (status);
			}
			if (not_found (status))
			{
				status = mdb_get (env.tx (transaction_a), state_blocks_v0, hash, value);
			}
			break;
		}
		case badem::block_type::invalid:
		case badem::block_type::not_a_block:
		{
			break;
		}
	}

	release_assert (success (status) || not_found (status));
	boost::optional<badem::mdb_val> result;
	if (success (status))
	{
		result = value;
	}
	return result;
}

badem::account badem::mdb_store::block_account_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	badem::block_sideband_v14 sideband;
	auto block (block_get_v14 (transaction_a, hash_a, &sideband));
	badem::account result (block->account ());
	if (result.is_zero ())
	{
		result = sideband.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return account containing hash
badem::account badem::mdb_store::block_account_computed_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	badem::account result (0);
	auto hash (hash_a);
	while (result.is_zero ())
	{
		auto block (block_get_v14 (transaction_a, hash));
		assert (block);
		result = block->account ();
		if (result.is_zero ())
		{
			auto type (badem::block_type::invalid);
			auto value (block_raw_get_v14 (transaction_a, block->previous (), type));
			if (entry_has_sideband_v14 (value.size (), type))
			{
				result = block_account_v14 (transaction_a, block->previous ());
			}
			else
			{
				badem::block_info block_info;
				if (!block_info_get (transaction_a, hash, block_info))
				{
					result = block_info.account;
				}
				else
				{
					result = frontier_get (transaction_a, hash);
					if (result.is_zero ())
					{
						auto successor (block_successor_v14 (transaction_a, hash));
						assert (!successor.is_zero ());
						hash = successor;
					}
				}
			}
		}
	}
	assert (!result.is_zero ());
	return result;
}

badem::uint128_t badem::mdb_store::block_balance_computed_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	summation_visitor visitor (transaction_a, *this, true);
	return visitor.compute_balance (hash_a);
}

std::shared_ptr<badem::block> badem::mdb_store::block_get_v14 (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_sideband_v14 * sideband_a, bool * is_state_v1) const
{
	badem::block_type type;
	auto value (block_raw_get_v14 (transaction_a, hash_a, type, is_state_v1));
	std::shared_ptr<badem::block> result;
	if (value.size () != 0)
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = badem::deserialize_block (stream, type);
		assert (result != nullptr);
		if (sideband_a)
		{
			sideband_a->type = type;
			if (full_sideband (transaction_a) || entry_has_sideband_v14 (value.size (), type))
			{
				bool error = sideband_a->deserialize (stream);
				(void)error;
				assert (!error);
			}
			else
			{
				// Reconstruct sideband data for block.
				sideband_a->account = block_account_computed_v14 (transaction_a, hash_a);
				sideband_a->balance = block_balance_computed_v14 (transaction_a, hash_a);
				sideband_a->successor = block_successor_v14 (transaction_a, hash_a);
				sideband_a->height = 0;
				sideband_a->timestamp = 0;
			}
		}
	}
	return result;
}

badem::mdb_store::upgrade_counters::upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1) :
before_v0 (count_before_v0),
before_v1 (count_before_v1)
{
}

bool badem::mdb_store::upgrade_counters::are_equal () const
{
	return (before_v0 == after_v0) && (before_v1 == after_v1);
}
