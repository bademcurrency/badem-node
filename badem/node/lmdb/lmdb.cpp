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
mdb_val::db_val (size_t size_a, void * data_a, badem::epoch epoch_a) :
value ({ size_a, data_a }),
epoch (epoch_a)
{
}

template <>
void mdb_val::convert_buffer_to_value ()
{
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}
}

badem::mdb_store::mdb_store (bool & error_a, badem::logger_mt & logger_a, boost::filesystem::path const & path_a, badem::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, bool drop_unchecked, size_t const batch_size) :
logger (logger_a),
env (error_a, path_a, lmdb_max_dbs, true),
mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error_a)
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
		// (can be a few minutes with the --fastbootstrap flag for instance)
		if (!is_fully_upgraded)
		{
			auto transaction (tx_begin_write ());
			open_databases (error_a, transaction, MDB_CREATE);
			if (!error_a)
			{
				error_a |= do_upgrades (transaction, batch_size);
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error_a, transaction, 0);
		}

		if (!error_a && drop_unchecked)
		{
			auto transaction (tx_begin_write ());
			unchecked_clear (transaction);
		}
	}
}

void badem::mdb_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

badem::write_transaction badem::mdb_store::tx_begin_write ()
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
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &accounts_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts_v1", flags, &accounts_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "send", flags, &send_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "receive", flags, &receive_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "open", flags, &open_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "change", flags, &change_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state", flags, &state_blocks_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state_v1", flags, &state_blocks_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending_v1", flags, &pending_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "representation", flags, &representation) != 0;
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
}

bool badem::mdb_store::do_upgrades (badem::write_transaction & transaction_a, size_t batch_size)
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
			upgrade_v12_to_v13 (transaction_a, batch_size);
		case 13:
			upgrade_v13_to_v14 (transaction_a);
		case 14:
			upgrade_v14_to_v15 (transaction_a);
		case 15:
			break;
		default:
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
			error = true;
			break;
	}
	return error;
}

void badem::mdb_store::upgrade_v1_to_v2 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	badem::account account (1);
	while (!account.is_zero ())
	{
		badem::mdb_iterator<badem::uint256_union, badem::account_info_v1> i (transaction_a, accounts_v0, badem::mdb_val (account));
		std::cerr << std::hex;
		if (i != badem::mdb_iterator<badem::uint256_union, badem::account_info_v1> (nullptr))
		{
			account = badem::uint256_union (i->first);
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

void badem::mdb_store::upgrade_v2_to_v3 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<badem::mdb_iterator<badem::account, badem::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		badem::account account_l ((*i)->first);
		badem::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<badem::mdb_iterator<badem::account, badem::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, badem::mdb_val (account_l), badem::mdb_val (sizeof (info), &info), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void badem::mdb_store::upgrade_v3_to_v4 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<badem::pending_key, badem::pending_info>> items;
	for (auto i (badem::store_iterator<badem::block_hash, badem::pending_info_v3> (std::make_unique<badem::mdb_iterator<badem::block_hash, badem::pending_info_v3>> (transaction_a, pending_v0))), n (badem::store_iterator<badem::block_hash, badem::pending_info_v3> (nullptr)); i != n; ++i)
	{
		badem::block_hash const & hash (i->first);
		badem::pending_info_v3 const & info (i->second);
		items.push (std::make_pair (badem::pending_key (info.destination, hash), badem::pending_info (info.source, info.amount, badem::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void badem::mdb_store::upgrade_v4_to_v5 (badem::transaction const & transaction_a)
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
				block_raw_put (transaction_a, vector, block->type (), badem::epoch::epoch_0, hash);
				if (!block->previous ().is_zero ())
				{
					badem::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					auto version (block_version (transaction_a, block->previous ()));
					assert (value.size () != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - badem::block_sideband::size (type));
					block_raw_put (transaction_a, data, type, version, block->previous ());
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void badem::mdb_store::upgrade_v5_to_v6 (badem::transaction const & transaction_a)
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

void badem::mdb_store::upgrade_v6_to_v7 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void badem::mdb_store::upgrade_v7_to_v8 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void badem::mdb_store::upgrade_v8_to_v9 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	badem::genesis genesis;
	std::shared_ptr<badem::block> block (std::move (genesis.open));
	badem::keypair junk;
	for (badem::mdb_iterator<badem::account, uint64_t> i (transaction_a, sequence), n (badem::mdb_iterator<badem::account, uint64_t> (nullptr)); i != n; ++i)
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

void badem::mdb_store::upgrade_v10_to_v11 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void badem::mdb_store::upgrade_v11_to_v12 (badem::transaction const & transaction_a)
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
			badem::store_iterator<badem::account, badem::account_info_v13> current (std::make_unique<badem::mdb_merge_iterator<badem::account, badem::account_info_v13>> (transaction_a, accounts_v0, accounts_v1, badem::mdb_val (account)));
			badem::store_iterator<badem::account, badem::account_info_v13> end (nullptr);
			if (current != end)
			{
				first = current->first;
				second = current->second;
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			badem::block_sideband sideband;
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
				auto block (block_get (transaction_a, hash, &sideband));
				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;
					block_put (transaction_a, hash, *block, sideband, block_version (transaction_a, hash));
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

void badem::mdb_store::upgrade_v13_to_v14 (badem::transaction const & transaction_a)
{
	// Upgrade all accounts to have a confirmation of 0 (except genesis which should have 1)
	version_put (transaction_a, 14);
	badem::store_iterator<badem::account, badem::account_info_v13> i (std::make_unique<badem::mdb_merge_iterator<badem::account, badem::account_info_v13>> (transaction_a, accounts_v0, accounts_v1));
	badem::store_iterator<badem::account, badem::account_info_v13> n (nullptr);

	std::vector<std::pair<badem::account, badem::account_info_v14>> account_infos;
	account_infos.reserve (account_count (transaction_a));
	for (; i != n; ++i)
	{
		badem::account_info_v13 const & account_info_v13 (i->second);
		uint64_t confirmation_height = 0;
		if (i->first == network_params.ledger.genesis_account)
		{
			confirmation_height = 1;
		}
		account_infos.emplace_back (i->first, badem::account_info_v14{ account_info_v13.head, account_info_v13.rep_block, account_info_v13.open_block, account_info_v13.balance, account_info_v13.modified, account_info_v13.block_count, confirmation_height, account_info_v13.epoch });
	}

	for (auto const & account_info : account_infos)
	{
		auto status1 (mdb_put (env.tx (transaction_a), table_to_dbi (get_account_db (account_info.second.epoch)), badem::mdb_val (account_info.first), badem::mdb_val (account_info.second), 0));
		release_assert (status1 == 0);
	}

	logger.always_log ("Completed confirmation height upgrade");

	badem::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, badem::mdb_val (node_id_mdb_key), nullptr));
	release_assert (!error || error == MDB_NOTFOUND);
}

void badem::mdb_store::upgrade_v14_to_v15 (badem::transaction const & transaction_a)
{
	version_put (transaction_a, 15);

	// Move confirmation height from account_info database to its own table
	std::vector<std::pair<badem::account, badem::account_info>> account_infos;
	account_infos.reserve (account_count (transaction_a));

	badem::store_iterator<badem::account, badem::account_info_v14> i (std::make_unique<badem::mdb_merge_iterator<badem::account, badem::account_info_v14>> (transaction_a, accounts_v0, accounts_v1));
	badem::store_iterator<badem::account, badem::account_info_v14> n (nullptr);
	for (; i != n; ++i)
	{
		auto const & account_info_v14 (i->second);
		account_infos.emplace_back (i->first, badem::account_info{ account_info_v14.head, account_info_v14.rep_block, account_info_v14.open_block, account_info_v14.balance, account_info_v14.modified, account_info_v14.block_count, account_info_v14.epoch });
		confirmation_height_put (transaction_a, i->first, i->second.confirmation_height);
	}

	for (auto const & account_info : account_infos)
	{
		account_put (transaction_a, account_info.first, account_info.second);
	}
}

void badem::mdb_store::version_put (badem::transaction const & transaction_a, int version_a)
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

int badem::mdb_store::put (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a, const badem::mdb_val & value_a) const
{
	return (mdb_put (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a, 0));
}

int badem::mdb_store::del (badem::transaction const & transaction_a, tables table_a, badem::mdb_val const & key_a) const
{
	return (mdb_del (env.tx (transaction_a), table_to_dbi (table_a), key_a, nullptr));
}

int badem::mdb_store::drop (badem::transaction const & transaction_a, tables table_a)
{
	return clear (transaction_a, table_to_dbi (table_a));
}

int badem::mdb_store::clear (badem::transaction const & transaction_a, MDB_dbi handle_a)
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
		case tables::accounts_v0:
			return accounts_v0;
		case tables::accounts_v1:
			return accounts_v1;
		case tables::send_blocks:
			return send_blocks;
		case tables::receive_blocks:
			return receive_blocks;
		case tables::open_blocks:
			return open_blocks;
		case tables::change_blocks:
			return change_blocks;
		case tables::state_blocks_v0:
			return state_blocks_v0;
		case tables::state_blocks_v1:
			return state_blocks_v1;
		case tables::pending_v0:
			return pending_v0;
		case tables::pending_v1:
			return pending_v1;
		case tables::blocks_info:
			return blocks_info;
		case tables::representation:
			return representation;
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
	return (MDB_NOTFOUND == status);
}

bool badem::mdb_store::success (int status) const
{
	return (MDB_SUCCESS == status);
}

int badem::mdb_store::status_code_not_found () const
{
	return MDB_NOTFOUND;
}
