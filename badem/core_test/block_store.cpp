#include <badem/core_test/testutil.hpp>
#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/utility.hpp>
#include <badem/node/common.hpp>
#include <badem/node/node.hpp>
#include <badem/secure/versioning.hpp>

#if BADEM_ROCKSDB
#include <badem/node/rocksdb/rocksdb.hpp>
#endif

#include <gtest/gtest.h>

#include <fstream>

#include <stdlib.h>

namespace
{
void modify_account_info_to_v13 (badem::mdb_store & store, badem::transaction const & transaction_a, badem::account const & account_a, badem::block_hash const & rep_block);
void modify_account_info_to_v14 (badem::mdb_store & store, badem::transaction const & transaction_a, badem::account const & account_a, uint64_t confirmation_height, badem::block_hash const & rep_block);
void modify_genesis_account_info_to_v5 (badem::mdb_store & store, badem::transaction const & transaction_a);
void write_sideband_v12 (badem::mdb_store & store_a, badem::transaction & transaction_a, badem::block & block_a, badem::block_hash const & successor_a, MDB_dbi db_a);
void write_sideband_v14 (badem::mdb_store & store_a, badem::transaction & transaction_a, badem::block const & block_a, MDB_dbi db_a);
}

TEST (block_store, construction)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
}

TEST (block_store, sideband_serialization)
{
	badem::block_sideband sideband1;
	sideband1.type = badem::block_type::receive;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		badem::vectorstream stream1 (vector);
		sideband1.serialize (stream1);
	}
	badem::bufferstream stream2 (vector.data (), vector.size ());
	badem::block_sideband sideband2;
	sideband2.type = badem::block_type::receive;
	ASSERT_FALSE (sideband2.deserialize (stream2));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::open_block block (0, 1, 0, badem::keypair ().prv, 0, 0);
	auto hash1 (block.hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block_exists (transaction, hash1));
	badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hash1, block, sideband);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store->block_exists (transaction, hash1));
	ASSERT_FALSE (store->block_exists (transaction, hash1.number () - 1));
	store->block_del (transaction, hash1);
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::open_block block1 (0, 1, 0, badem::keypair ().prv, 0, 0);
	auto transaction (store->tx_begin_write ());
	badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, block1.hash (), block1, sideband);
	badem::open_block block2 (0, 2, 0, badem::keypair ().prv, 0, 0);
	store->block_put (transaction, block2.hash (), block2, sideband);
	ASSERT_NE (nullptr, store->block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (0, sideband.successor.number ());
	sideband.successor = block2.hash ();
	store->block_put (transaction, block1.hash (), block1, sideband);
	ASSERT_NE (nullptr, store->block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (block2.hash (), sideband.successor);
	store->block_successor_clear (transaction, block1.hash ());
	ASSERT_NE (nullptr, store->block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (0, sideband.successor.number ());
}

TEST (block_store, add_nonempty_block)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key1;
	badem::open_block block (0, 1, 0, badem::keypair ().prv, 0, 0);
	auto hash1 (block.hash ());
	block.signature = badem::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hash1, block, sideband);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key1;
	badem::open_block block (0, 1, 1, badem::keypair ().prv, 0, 0);
	auto hash1 (block.hash ());
	block.signature = badem::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	badem::open_block block2 (0, 1, 3, badem::keypair ().prv, 0, 0);
	block2.hashables.account = 3;
	auto hash2 (block2.hash ());
	block2.signature = badem::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block_get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hash1, block, sideband);
	badem::block_sideband sideband2 (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hash2, block2, sideband2);
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store->block_get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key1;
	badem::keypair key2;
	badem::open_block block1 (0, 1, 0, badem::keypair ().prv, 0, 0);
	auto transaction (store->tx_begin_write ());
	badem::block_sideband sideband1 (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, block1.hash (), block1, sideband1);
	badem::receive_block block (block1.hash (), 1, badem::keypair ().prv, 2, 3);
	badem::block_hash hash1 (block.hash ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	badem::block_sideband sideband (badem::block_type::receive, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hash1, block, sideband);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key1;
	badem::pending_key key2 (0, 0);
	badem::pending_info pending1;
	auto transaction (store->tx_begin_write ());
	ASSERT_TRUE (store->pending_get (transaction, key2, pending1));
	store->pending_put (transaction, key2, pending1);
	badem::pending_info pending2;
	ASSERT_FALSE (store->pending_get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store->pending_del (transaction, key2);
	ASSERT_TRUE (store->pending_get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending_end (), store->pending_begin (transaction));
	store->pending_put (transaction, badem::pending_key (1, 2), { 2, 3, badem::epoch::epoch_1 });
	auto current (store->pending_begin (transaction));
	ASSERT_NE (store->pending_end (), current);
	badem::pending_key key1 (current->first);
	ASSERT_EQ (badem::account (1), key1.account);
	ASSERT_EQ (badem::block_hash (2), key1.hash);
	badem::pending_info pending (current->second);
	ASSERT_EQ (badem::account (2), pending.source);
	ASSERT_EQ (badem::amount (3), pending.amount);
	ASSERT_EQ (badem::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::stat stats;
	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending_put (transaction, badem::pending_key (badem::account (3), badem::block_hash (1)), badem::pending_info (badem::account (10), badem::amount (1), badem::epoch::epoch_0));
	store->pending_put (transaction, badem::pending_key (badem::account (3), badem::block_hash (4)), badem::pending_info (badem::account (10), badem::amount (0), badem::epoch::epoch_0));
	// Populate pending_v1
	store->pending_put (transaction, badem::pending_key (badem::account (2), badem::block_hash (2)), badem::pending_info (badem::account (10), badem::amount (2), badem::epoch::epoch_1));
	store->pending_put (transaction, badem::pending_key (badem::account (2), badem::block_hash (3)), badem::pending_info (badem::account (10), badem::amount (3), badem::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		badem::account begin (3);
		badem::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, badem::pending_key (begin, 0))), n (store->pending_begin (transaction, badem::pending_key (end, 0))); i != n; ++i, ++count)
		{
			badem::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		badem::account begin (2);
		badem::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, badem::pending_key (begin, 0))), n (store->pending_begin (transaction, badem::pending_key (end, 0))); i != n; ++i, ++count)
		{
			badem::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::genesis genesis;
	auto hash (genesis.hash ());
	badem::rep_weights rep_weights;
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count_cache{ 0 };
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
	badem::account_info info;
	ASSERT_FALSE (store->account_get (transaction, badem::genesis_account, info));
	ASSERT_EQ (hash, info.head);
	auto block1 (store->block_get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<badem::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, badem::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	uint64_t confirmation_height;
	ASSERT_FALSE (store->confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (confirmation_height, 1);
	auto test_pub_text (badem::test_genesis_key.pub.to_string ());
	auto test_pub_account (badem::test_genesis_key.pub.to_account ());
	auto test_prv_text (badem::test_genesis_key.prv.data.to_string ());
	ASSERT_EQ (badem::genesis_account, badem::test_genesis_key.pub);
}

TEST (bootstrap, simple)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<badem::send_block> (0, 1, 2, badem::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	ASSERT_EQ (*block1, *(block3[0].block));
	store->unchecked_del (transaction, badem::unchecked_key (block1->previous (), block1->hash ()));
	auto block4 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block4.empty ());
}

TEST (unchecked, multiple)
{
	badem::logger_mt logger;
	badem::mdb_store store (logger, badem::unique_path ());
	ASSERT_TRUE (!store.init_error ());
	auto block1 (std::make_shared<badem::send_block> (4, 1, 2, badem::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->source (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	auto block4 (store.unchecked_get (transaction, block1->source ()));
	ASSERT_FALSE (block4.empty ());
}

TEST (unchecked, double_put)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<badem::send_block> (4, 1, 2, badem::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (block3.size (), 1);
}

TEST (unchecked, multiple_get)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<badem::send_block> (4, 1, 2, badem::keypair ().prv, 4, 5));
	auto block2 (std::make_shared<badem::send_block> (3, 1, 2, badem::keypair ().prv, 4, 5));
	auto block3 (std::make_shared<badem::send_block> (5, 1, 2, badem::keypair ().prv, 4, 5));
	{
		auto transaction (store->tx_begin_write ());
		store->unchecked_put (transaction, block1->previous (), block1); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block1); // unchecked2
		store->unchecked_put (transaction, block2->previous (), block2); // unchecked3
		store->unchecked_put (transaction, block1->previous (), block2); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block2); // unchecked2
		store->unchecked_put (transaction, block3->previous (), block3);
		store->unchecked_put (transaction, block3->hash (), block3); // unchecked4
		store->unchecked_put (transaction, block1->previous (), block3); // unchecked1
	}
	auto transaction (store->tx_begin_read ());
	auto unchecked_count (store->unchecked_count (transaction));
	ASSERT_EQ (unchecked_count, 8);
	std::vector<badem::block_hash> unchecked1;
	auto unchecked1_blocks (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<badem::block_hash> unchecked2;
	auto unchecked2_blocks (store->unchecked_get (transaction, block1->hash ()));
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	auto unchecked3 (store->unchecked_get (transaction, block2->previous ()));
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	auto unchecked4 (store->unchecked_get (transaction, block3->hash ()));
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	auto unchecked5 (store->unchecked_get (transaction, block2->hash ()));
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (block_store, empty_accounts)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::open_block block1 (0, 1, 0, badem::keypair ().prv, 0, 0);
	auto transaction (store->tx_begin_write ());
	badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, block1.hash (), block1, sideband);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<badem::send_block> (0, 1, 2, badem::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	store->unchecked_put (transaction, block1->hash (), block1);
	store->flush (transaction);
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_NE (end, begin);
	auto hash1 (begin->first.key ());
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks (store->unchecked_get (transaction, hash1));
	ASSERT_EQ (1, blocks.size ());
	auto block2 (blocks[0].block);
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key0;
	badem::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	badem::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::account account1 (0);
	badem::account_info info1 (0, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, 0);
	store->account_put (transaction, account1, info1);
	badem::account_info info2;
	store->account_get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::account account (0);
	badem::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account, 20);
	store->account_put (transaction, account, { hash, account, hash, 42, 100, 200, badem::epoch::epoch_0 });
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, badem::account (begin->first));
	badem::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	uint64_t confirmation_height;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account, confirmation_height));
	ASSERT_EQ (20, confirmation_height);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::open_block block1 (0, 1, 1, badem::keypair ().prv, 0, 0);
	block1.hashables.account = 1;
	std::vector<badem::block_hash> hashes;
	std::vector<badem::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store->tx_begin_write ());
	badem::block_sideband sideband1 (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hashes[0], block1, sideband1);
	badem::open_block block2 (0, 1, 2, badem::keypair ().prv, 0, 0);
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	badem::block_sideband sideband2 (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, hashes[1], block2, sideband2);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::account account1 (1);
	badem::block_hash hash1 (2);
	badem::account account2 (3);
	badem::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, 20);
	store->account_put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, badem::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, 30);
	store->account_put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, badem::epoch::epoch_0 });
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, badem::account (begin->first));
	badem::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	uint64_t confirmation_height;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account1, confirmation_height));
	ASSERT_EQ (20, confirmation_height);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, badem::account (begin->first));
	badem::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height_get (transaction, account2, confirmation_height));
	ASSERT_EQ (30, confirmation_height);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::account account1 (1);
	badem::block_hash hash1 (2);
	badem::account account2 (3);
	badem::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, 0);
	store->account_put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, badem::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, 0);
	store->account_put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, badem::epoch::epoch_0 });
	auto first (store->latest_begin (transaction));
	auto second (store->latest_begin (transaction));
	++second;
	auto find1 (store->latest_begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->latest_begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->latest_begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (mdb_block_store, bad_path)
{
	badem::logger_mt logger;
	badem::mdb_store store (logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (store.init_error ());
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (badem::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	badem::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	badem::logger_mt logger;
	auto store = badem::make_store (logger, path);
	ASSERT_TRUE (store->init_error ());
}

TEST (block_store, roots)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::send_block send_block (0, 1, 2, badem::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	badem::change_block change_block (0, 1, badem::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	badem::receive_block receive_block (0, 1, badem::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	badem::open_block open_block (0, 1, 2, badem::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::pending_key two (2, 0);
	badem::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending_put (transaction, two, pending);
	badem::pending_key one (1, 0);
	ASSERT_FALSE (store->pending_exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::account two (2);
	badem::account_info info;
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, two, 0);
	store->account_put (transaction, two, info);
	badem::account one (1);
	ASSERT_FALSE (store->account_exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	std::unordered_set<badem::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		badem::account account;
		badem::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->confirmation_height_put (transaction, account, 0);
		store->account_put (transaction, account, badem::account_info ());
	}
	std::unordered_set<badem::account> accounts2;
	badem::account previous (0);
	auto transaction (store->tx_begin_read ());
	for (auto i (store->latest_begin (transaction, 0)), n (store->latest_end ()); i != n; ++i)
	{
		badem::account current (i->first);
		assert (current.number () > previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
}

TEST (block_store, frontier)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	badem::block_hash hash (100);
	badem::account account (200);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
	store->frontier_put (transaction, hash, account);
	ASSERT_EQ (account, store->frontier_get (transaction, hash));
	store->frontier_del (transaction, hash);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::send_block send1 (0, 0, 0, badem::keypair ().prv, 0, 1);
	badem::send_block send2 (0, 0, 0, badem::keypair ().prv, 0, 2);
	auto transaction (store->tx_begin_write ());
	badem::block_sideband sideband1 (badem::block_type::send, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, 0, send1, sideband1);
	badem::block_sideband sideband2 (badem::block_type::send, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
	store->block_put (transaction, 0, send2, sideband2);
	auto block3 (store->block_get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block_count (transaction).sum ());
		badem::open_block block (0, 1, 0, badem::keypair ().prv, 0, 0);
		auto hash1 (block.hash ());
		badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
		store->block_put (transaction, hash1, block, sideband);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block_count (transaction).sum ());
}

TEST (block_store, account_count)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account_count (transaction));
		badem::account account (200);
		store->confirmation_height_put (transaction, account, 0);
		store->account_put (transaction, account, badem::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account_count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	badem::genesis genesis;
	badem::rep_weights rep_weights;
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count_cache{ 0 };
	store->initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
	ASSERT_EQ (1, cemented_count);
}

TEST (block_store, sequence_increment)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::keypair key1;
	badem::keypair key2;
	auto block1 (std::make_shared<badem::open_block> (0, 1, 0, badem::keypair ().prv, 0, 0));
	auto transaction (store->tx_begin_write ());
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (1, vote1->sequence);
	auto vote2 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (2, vote2->sequence);
	auto vote3 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (1, vote3->sequence);
	auto vote4 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (2, vote4->sequence);
	vote1->sequence = 20;
	auto seq5 (store->vote_max (transaction, vote1));
	ASSERT_EQ (20, seq5->sequence);
	vote3->sequence = 30;
	auto seq6 (store->vote_max (transaction, vote3));
	ASSERT_EQ (30, seq6->sequence);
	auto vote5 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (21, vote5->sequence);
	auto vote6 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (31, vote6->sequence);
}

TEST (mdb_block_store, upgrade_v2_v3)
{
	badem::keypair key1;
	badem::keypair key2;
	badem::block_hash change_hash;
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_TRUE (!store.init_error ());
		auto transaction (store.tx_begin_write ());
		badem::genesis genesis;
		auto hash (genesis.hash ());
		badem::stat stats;
		badem::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		badem::work_pool pool (std::numeric_limits<unsigned>::max ());
		badem::change_block change (hash, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (hash));
		change_hash = change.hash ();
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (0, ledger.weight (badem::test_genesis_key.pub));
		ASSERT_EQ (badem::genesis_amount, ledger.weight (key1.pub));
		store.version_put (transaction, 2);
		ledger.rep_weights.representation_put (key1.pub, 7);
		ASSERT_EQ (7, ledger.weight (key1.pub));
		ASSERT_EQ (2, store.version_get (transaction));
		ledger.rep_weights.representation_put (key2.pub, 6);
		ASSERT_EQ (6, ledger.weight (key2.pub));
		badem::account_info info;
		ASSERT_FALSE (store.account_get (transaction, badem::test_genesis_key.pub, info));
		auto rep_block = ledger.representative (transaction, ledger.latest (transaction, badem::test_genesis_key.pub));
		badem::account_info_v5 info_old (info.head, rep_block, info.open_block, info.balance, info.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, badem::mdb_val (badem::test_genesis_key.pub), badem::mdb_val (sizeof (info_old), &info_old), 0));
		(void)status;
		assert (status == 0);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	badem::stat stats;
	badem::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (!store.init_error ());
	ASSERT_LT (2, store.version_get (transaction));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (key1.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	badem::account_info info;
	ASSERT_FALSE (store.account_get (transaction, badem::test_genesis_key.pub, info));
	ASSERT_EQ (change_hash, ledger.representative (transaction, ledger.latest (transaction, badem::test_genesis_key.pub)));
}

TEST (mdb_block_store, upgrade_v3_v4)
{
	badem::keypair key1;
	badem::keypair key2;
	badem::keypair key3;
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 3);
		badem::pending_info_v3 info (key1.pub, 100, key2.pub);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, badem::mdb_val (key3.pub), badem::mdb_val (sizeof (info), &info), 0));
		ASSERT_EQ (0, status);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	badem::stat stats;
	badem::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_FALSE (store.init_error ());
	ASSERT_LT (3, store.version_get (transaction));
	badem::pending_key key (key2.pub, reinterpret_cast<badem::block_hash const &> (key3.pub));
	badem::pending_info info;
	auto error (store.pending_get (transaction, key, info));
	ASSERT_FALSE (error);
	ASSERT_EQ (key1.pub, info.source);
	ASSERT_EQ (badem::amount (100), info.amount);
	ASSERT_EQ (badem::epoch::epoch_0, info.epoch);
}

TEST (mdb_block_store, upgrade_v4_v5)
{
	badem::block_hash genesis_hash (0);
	badem::block_hash hash (0);
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		badem::genesis genesis;
		badem::stat stats;
		badem::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		store.version_put (transaction, 4);
		badem::account_info info;
		ASSERT_FALSE (store.account_get (transaction, badem::test_genesis_key.pub, info));
		badem::keypair key0;
		badem::work_pool pool (std::numeric_limits<unsigned>::max ());
		badem::send_block block0 (info.head, key0.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (info.head));
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block0).code);
		hash = block0.hash ();
		auto original (store.block_get (transaction, info.head));
		genesis_hash = info.head;
		store.block_successor_clear (transaction, info.head);
		ASSERT_TRUE (store.block_successor (transaction, genesis_hash).is_zero ());
		modify_genesis_account_info_to_v5 (store, transaction);
		// The pending send needs to be the correct version
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, badem::mdb_val (badem::pending_key (key0.pub, block0.hash ())), badem::mdb_val (badem::pending_info_v14 (badem::genesis_account, badem::Gbdm_ratio, badem::epoch::epoch_0)), 0));
		ASSERT_EQ (status, MDB_SUCCESS);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (hash, store.block_successor (transaction, genesis_hash));
}

TEST (block_store, block_random)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	badem::genesis genesis;
	{
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
	}
	auto transaction (store->tx_begin_read ());
	auto block (store->block_random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *genesis.open);
}

TEST (mdb_block_store, upgrade_v5_v6)
{
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		badem::genesis genesis;
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store.initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		store.version_put (transaction, 5);
		modify_genesis_account_info_to_v5 (store, transaction);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	badem::account_info info;
	store.account_get (transaction, badem::test_genesis_key.pub, info);
	ASSERT_EQ (1, info.block_count);
}

TEST (mdb_block_store, upgrade_v6_v7)
{
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		badem::genesis genesis;
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store.initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		store.version_put (transaction, 6);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, genesis.open->hash ());
		auto send1 (std::make_shared<badem::send_block> (0, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
		store.unchecked_put (transaction, send1->hash (), send1);
		store.flush (transaction);
		ASSERT_NE (store.unchecked_end (), store.unchecked_begin (transaction));
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.unchecked_end (), store.unchecked_begin (transaction));
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, DISABLED_change_dupsort) // Unchecked is no longer dupsort table
{
	auto path (badem::unique_path ());
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
	auto send1 (std::make_shared<badem::send_block> (0, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<badem::send_block> (1, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	ASSERT_NE (send1->hash (), send2->hash ());
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 0));
	mdb_dbi_close (store.env, store.unchecked);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (mdb_block_store, upgrade_v7_v8)
{
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
		store.version_put (transaction, 7);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_write ());
	auto send1 (std::make_shared<badem::send_block> (0, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<badem::send_block> (1, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, sequence_flush)
{
	auto path (badem::unique_path ());
	badem::logger_mt logger;
	auto store = badem::make_store (logger, path);
	ASSERT_FALSE (store->init_error ());
	auto transaction (store->tx_begin_write ());
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (0, 0, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, send1));
	auto seq2 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store->flush (transaction);
	auto seq3 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

TEST (block_store, sequence_flush_by_hash)
{
	auto path (badem::unique_path ());
	badem::logger_mt logger;
	auto store = badem::make_store (logger, path);
	ASSERT_FALSE (store->init_error ());
	auto transaction (store->tx_begin_write ());
	badem::keypair key1;
	std::vector<badem::block_hash> blocks1;
	blocks1.push_back (badem::genesis ().hash ());
	blocks1.push_back (1234);
	blocks1.push_back (5678);
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, blocks1));
	auto seq2 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store->flush (transaction);
	auto seq3 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

// Upgrading tracking block sequence numbers to whole vote.
TEST (mdb_block_store, upgrade_v8_v9)
{
	auto path (badem::unique_path ());
	badem::keypair key;
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.vote, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "sequence", MDB_CREATE, &store.vote));
		uint64_t sequence (10);
		ASSERT_EQ (0, mdb_put (store.env.tx (transaction), store.vote, badem::mdb_val (key.pub), badem::mdb_val (sizeof (sequence), &sequence), 0));
		store.version_put (transaction, 8);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (8, store.version_get (transaction));
	auto vote (store.vote_get (transaction, key.pub));
	ASSERT_NE (nullptr, vote);
	ASSERT_EQ (10, vote->sequence);
}

TEST (block_store, state_block)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_FALSE (store->init_error ());
	badem::genesis genesis;
	badem::keypair key1;
	badem::state_block block1 (1, genesis.hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	{
		auto transaction (store->tx_begin_write ());
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store->initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		ASSERT_EQ (badem::block_type::state, block1.type ());
		badem::block_sideband sideband1 (badem::block_type::state, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
		store->block_put (transaction, block1.hash (), block1, sideband1);
		ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
		auto block2 (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block_count (transaction));
		ASSERT_EQ (1, count.state);
		store->block_del (transaction, block1.hash ());
		ASSERT_FALSE (store->block_exists (transaction, block1.hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block_count (transaction));
	ASSERT_EQ (0, count2.state);
}

TEST (mdb_block_store, upgrade_sideband_genesis)
{
	badem::genesis genesis;
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store.initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, genesis.open->hash ());
		badem::block_sideband sideband;
		auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (1, sideband.height);
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		write_sideband_v12 (store, transaction, *genesis_block, 0, store.open_blocks);
		badem::block_sideband_v14 sideband1;
		auto genesis_block2 (store.block_get_v14 (transaction, genesis.hash (), &sideband1));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (0, sideband1.height);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	badem::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
}

TEST (mdb_block_store, upgrade_sideband_two_blocks)
{
	badem::genesis genesis;
	badem::block_hash hash2;
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		badem::stat stat;
		badem::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		badem::work_pool pool (std::numeric_limits<unsigned>::max ());
		badem::state_block block (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block.hash ();
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block).code);
		store.block_del (transaction, hash2);
		mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
		mdb_dbi_open (store.env.tx (transaction), "state", MDB_CREATE, &store.state_blocks_v0);
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, hash2);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, badem::mdb_val (badem::pending_key (badem::test_genesis_key.pub, block.hash ())), badem::mdb_val (badem::pending_info_v14 (badem::genesis_account, badem::Gbdm_ratio, badem::epoch::epoch_0)), 0));
		ASSERT_EQ (status, MDB_SUCCESS);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	badem::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
	badem::block_sideband sideband2;
	auto block2 (store.block_get (transaction, hash2, &sideband2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, sideband2.height);
}

TEST (mdb_block_store, upgrade_sideband_two_accounts)
{
	badem::genesis genesis;
	badem::block_hash hash2;
	badem::block_hash hash3;
	badem::keypair key;
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		badem::stat stat;
		badem::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		badem::work_pool pool (std::numeric_limits<unsigned>::max ());
		badem::state_block block1 (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
		badem::state_block block2 (key.pub, 0, badem::test_genesis_key.pub, badem::Gbdm_ratio, hash2, key.prv, key.pub, *pool.generate (key.pub));
		hash3 = block2.hash ();
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
		store.block_del (transaction, hash2);
		store.block_del (transaction, hash3);
		mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
		mdb_dbi_open (store.env.tx (transaction), "state", MDB_CREATE, &store.state_blocks_v0);
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block1, 0, store.state_blocks_v0);
		write_sideband_v12 (store, transaction, block2, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, hash2);
		modify_account_info_to_v13 (store, transaction, block2.account (), hash3);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	badem::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
	badem::block_sideband sideband2;
	auto block2 (store.block_get (transaction, hash2, &sideband2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, sideband2.height);
	badem::block_sideband sideband3;
	auto block3 (store.block_get (transaction, hash3, &sideband3));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (1, sideband3.height);
}

TEST (mdb_block_store, insert_after_legacy)
{
	badem::logger_mt logger;
	badem::genesis genesis;
	badem::mdb_store store (logger, badem::unique_path ());
	ASSERT_FALSE (store.init_error ());
	badem::stat stat;
	badem::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.version_put (transaction, 11);
	store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
	mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
	write_sideband_v12 (store, transaction, *genesis.open, 0, store.open_blocks);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block block (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block).code);
}

// Account for an open block should be retrievable
TEST (mdb_block_store, legacy_account_computed)
{
	badem::logger_mt logger;
	badem::mdb_store store (logger, badem::unique_path ());
	ASSERT_TRUE (!store.init_error ());
	badem::stat stats;
	badem::ledger ledger (store, stats);
	badem::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
	store.version_put (transaction, 11);
	mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
	write_sideband_v12 (store, transaction, *genesis.open, 0, store.open_blocks);
	ASSERT_EQ (badem::genesis_account, ledger.account (transaction, genesis.hash ()));
}

TEST (mdb_block_store, upgrade_sideband_epoch)
{
	bool error (false);
	badem::genesis genesis;
	badem::block_hash hash2;
	auto path (badem::unique_path ());
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (error);
		badem::stat stat;
		badem::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		badem::state_block block1 (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount, ledger.epoch_link (badem::epoch::epoch_1), badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
		ASSERT_EQ (badem::epoch::epoch_1, store.block_version (transaction, hash2));
		store.block_del (transaction, hash2);
		store.block_del (transaction, genesis.open->hash ());
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block1, 0, store.state_blocks_v1);

		badem::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.state_blocks_v1, badem::mdb_val (hash2), value));
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.open_blocks, badem::mdb_val (genesis.open->hash ()), value));

		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, hash2);
		store.account_del (transaction, badem::genesis_account);
	}
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	badem::stat stat;
	badem::ledger ledger (store, stat);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (store.full_sideband (transaction));
	ASSERT_EQ (badem::epoch::epoch_1, store.block_version (transaction, hash2));
	badem::block_sideband sideband;
	auto block1 (store.block_get (transaction, hash2, &sideband));
	ASSERT_NE (0, sideband.height);
	badem::state_block block2 (badem::test_genesis_key.pub, hash2, badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (hash2));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (badem::epoch::epoch_1, store.block_version (transaction, block2.hash ()));
}

TEST (mdb_block_store, sideband_height)
{
	badem::logger_mt logger;
	badem::genesis genesis;
	badem::keypair key1;
	badem::keypair key2;
	badem::keypair key3;
	badem::mdb_store store (logger, badem::unique_path ());
	ASSERT_FALSE (store.init_error ());
	badem::stat stat;
	badem::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send (genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	badem::receive_block receive (send.hash (), send.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive).code);
	badem::change_block change (receive.hash (), 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (receive.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change).code);
	badem::state_block state_send1 (badem::test_genesis_key.pub, change.hash (), 0, badem::genesis_amount - badem::Gbdm_ratio, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (change.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_send1).code);
	badem::state_block state_send2 (badem::test_genesis_key.pub, state_send1.hash (), 0, badem::genesis_amount - 2 * badem::Gbdm_ratio, key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (state_send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_send2).code);
	badem::state_block state_send3 (badem::test_genesis_key.pub, state_send2.hash (), 0, badem::genesis_amount - 3 * badem::Gbdm_ratio, key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (state_send2.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_send3).code);
	badem::state_block state_open (key1.pub, 0, 0, badem::Gbdm_ratio, state_send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_open).code);
	badem::state_block epoch (key1.pub, state_open.hash (), 0, badem::Gbdm_ratio, ledger.epoch_link (badem::epoch::epoch_1), badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (state_open.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch).code);
	ASSERT_EQ (badem::epoch::epoch_1, store.block_version (transaction, epoch.hash ()));
	badem::state_block epoch_open (key2.pub, 0, 0, 0, ledger.epoch_link (badem::epoch::epoch_1), badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (badem::epoch::epoch_1, store.block_version (transaction, epoch_open.hash ()));
	badem::state_block state_receive (key2.pub, epoch_open.hash (), 0, badem::Gbdm_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (epoch_open.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_receive).code);
	badem::open_block open (state_send3.hash (), badem::test_genesis_key.pub, key3.pub, key3.prv, key3.pub, *pool.generate (key3.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open).code);
	badem::block_sideband sideband1;
	auto block1 (store.block_get (transaction, genesis.hash (), &sideband1));
	ASSERT_EQ (sideband1.height, 1);
	badem::block_sideband sideband2;
	auto block2 (store.block_get (transaction, send.hash (), &sideband2));
	ASSERT_EQ (sideband2.height, 2);
	badem::block_sideband sideband3;
	auto block3 (store.block_get (transaction, receive.hash (), &sideband3));
	ASSERT_EQ (sideband3.height, 3);
	badem::block_sideband sideband4;
	auto block4 (store.block_get (transaction, change.hash (), &sideband4));
	ASSERT_EQ (sideband4.height, 4);
	badem::block_sideband sideband5;
	auto block5 (store.block_get (transaction, state_send1.hash (), &sideband5));
	ASSERT_EQ (sideband5.height, 5);
	badem::block_sideband sideband6;
	auto block6 (store.block_get (transaction, state_send2.hash (), &sideband6));
	ASSERT_EQ (sideband6.height, 6);
	badem::block_sideband sideband7;
	auto block7 (store.block_get (transaction, state_send3.hash (), &sideband7));
	ASSERT_EQ (sideband7.height, 7);
	badem::block_sideband sideband8;
	auto block8 (store.block_get (transaction, state_open.hash (), &sideband8));
	ASSERT_EQ (sideband8.height, 1);
	badem::block_sideband sideband9;
	auto block9 (store.block_get (transaction, epoch.hash (), &sideband9));
	ASSERT_EQ (sideband9.height, 2);
	badem::block_sideband sideband10;
	auto block10 (store.block_get (transaction, epoch_open.hash (), &sideband10));
	ASSERT_EQ (sideband10.height, 1);
	badem::block_sideband sideband11;
	auto block11 (store.block_get (transaction, state_receive.hash (), &sideband11));
	ASSERT_EQ (sideband11.height, 2);
	badem::block_sideband sideband12;
	auto block12 (store.block_get (transaction, open.hash (), &sideband12));
	ASSERT_EQ (sideband12.height, 1);
}

TEST (block_store, peers)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	badem::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
		ASSERT_EQ (store->peer_count (transaction), 0);

		// Add one
		store->peer_put (transaction, endpoint);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Add another one and check that it (and the existing one) can be found
	badem::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer_put (transaction, endpoint1);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 2);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint1);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::address_v6::from_string ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	badem::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		badem::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	badem::bufferstream stream1 (bytes.data (), bytes.size ());
	badem::endpoint_key endpoint_key1;
	badem::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_FALSE (store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight_count (transaction));
		ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
		store->online_weight_put (transaction, 1, 2);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (1, store->online_weight_count (transaction));
		auto item (store->online_weight_begin (transaction));
		ASSERT_NE (store->online_weight_end (), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		store->online_weight_del (transaction, 1);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight_count (transaction));
	ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
}

// Adding confirmation height to accounts
TEST (mdb_block_store, upgrade_v13_v14)
{
	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::genesis genesis;
		badem::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store.initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		badem::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, badem::genesis_account, account_info));
		uint64_t confirmation_height;
		ASSERT_FALSE (store.confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
		ASSERT_EQ (confirmation_height, 1);
		store.version_put (transaction, 13);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, genesis.open->hash ());

		// This should fail as sizes are no longer correct for account_info_v14
		badem::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v0, badem::mdb_val (badem::genesis_account), value));
		badem::account_info_v14 info;
		ASSERT_NE (value.size (), info.db_size ());
	}

	// Now do the upgrade
	badem::logger_mt logger;
	auto error (false);
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	badem::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v0, badem::mdb_val (badem::genesis_account), value));
	badem::account_info info;
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist and be correct
	uint64_t confirmation_height;
	ASSERT_FALSE (store.confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (confirmation_height, 1);

	// Test deleting node ID
	badem::uint256_union node_id_mdb_key (3);
	auto error_node_id (mdb_get (store.env.tx (transaction), store.meta, badem::mdb_val (node_id_mdb_key), value));
	ASSERT_EQ (error_node_id, MDB_NOTFOUND);

	ASSERT_LT (13, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v14_v15)
{
	// Extract confirmation height to a separate database
	auto path (badem::unique_path ());
	badem::genesis genesis;
	badem::network_params network_params;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send (genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	badem::state_block epoch (badem::test_genesis_key.pub, send.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio, network_params.ledger.epochs.link (badem::epoch::epoch_1), badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (send.hash ()));
	badem::state_block state_send (badem::test_genesis_key.pub, epoch.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::Gbdm_ratio * 2, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, *pool.generate (epoch.hash ()));
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		badem::stat stats;
		badem::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		badem::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, badem::genesis_account, account_info));
		uint64_t confirmation_height;
		ASSERT_FALSE (store.confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
		ASSERT_EQ (confirmation_height, 1);
		// These databases get remove after an upgrade, so readd them
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "pending_v1", MDB_CREATE, &store.pending_v1));
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, state_send).code);
		// Lower the database to the previous version
		store.version_put (transaction, 14);
		store.confirmation_height_del (transaction, badem::genesis_account);
		modify_account_info_to_v14 (store, transaction, badem::genesis_account, confirmation_height, state_send.hash ());

		store.pending_del (transaction, badem::pending_key (badem::genesis_account, state_send.hash ()));

		write_sideband_v14 (store, transaction, state_send, store.state_blocks_v1);
		write_sideband_v14 (store, transaction, epoch, store.state_blocks_v1);

		// Remove from state table
		store.block_del (transaction, state_send.hash ());
		store.block_del (transaction, epoch.hash ());

		// Turn pending into v14
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v0, badem::mdb_val (badem::pending_key (badem::test_genesis_key.pub, send.hash ())), badem::mdb_val (badem::pending_info_v14 (badem::genesis_account, badem::Gbdm_ratio, badem::epoch::epoch_0)), 0));
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v1, badem::mdb_val (badem::pending_key (badem::test_genesis_key.pub, state_send.hash ())), badem::mdb_val (badem::pending_info_v14 (badem::genesis_account, badem::Gbdm_ratio, badem::epoch::epoch_1)), 0));

		// This should fail as sizes are no longer correct for account_info
		badem::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v1, badem::mdb_val (badem::genesis_account), value));
		badem::account_info info;
		ASSERT_NE (value.size (), info.db_size ());
		store.account_del (transaction, badem::genesis_account);

		// Confirmation height for the account should be deleted
		ASSERT_TRUE (store.confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	}

	// Now do the upgrade
	badem::logger_mt logger;
	auto error (false);
	badem::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	badem::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts, badem::mdb_val (badem::genesis_account), value));
	badem::account_info info (value);
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist
	uint64_t confirmation_height;
	ASSERT_FALSE (store.confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (confirmation_height, 1);

	// The representation table should be deleted
	auto error_get_representation (mdb_get (store.env.tx (transaction), store.representation, badem::mdb_val (badem::genesis_account), value));
	ASSERT_NE (error_get_representation, MDB_SUCCESS);
	ASSERT_EQ (store.representation, 0);

	// accounts_v1, state_blocks_v1 & pending_v1 tables should be deleted
	auto error_get_accounts_v1 (mdb_get (store.env.tx (transaction), store.accounts_v1, badem::mdb_val (badem::genesis_account), value));
	ASSERT_NE (error_get_accounts_v1, MDB_SUCCESS);
	auto error_get_pending_v1 (mdb_get (store.env.tx (transaction), store.pending_v1, badem::mdb_val (badem::pending_key (badem::test_genesis_key.pub, state_send.hash ())), value));
	ASSERT_NE (error_get_pending_v1, MDB_SUCCESS);
	auto error_get_state_v1 (mdb_get (store.env.tx (transaction), store.state_blocks_v1, badem::mdb_val (state_send.hash ()), value));
	ASSERT_NE (error_get_state_v1, MDB_SUCCESS);

	// Check that the epochs are set correctly for the sideband, accounts and pending entries
	badem::block_sideband sideband;
	auto block = store.block_get (transaction, state_send.hash (), &sideband);
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (sideband.epoch, badem::epoch::epoch_1);
	block = store.block_get (transaction, send.hash (), &sideband);
	ASSERT_NE (block, nullptr);
	badem::block_sideband sideband1;
	ASSERT_EQ (sideband1.epoch, badem::epoch::epoch_0);
	ASSERT_EQ (info.epoch (), badem::epoch::epoch_1);
	badem::pending_info pending_info;
	store.pending_get (transaction, badem::pending_key (badem::test_genesis_key.pub, send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, badem::epoch::epoch_0);
	store.pending_get (transaction, badem::pending_key (badem::test_genesis_key.pub, state_send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, badem::epoch::epoch_1);

	// Version should be correct
	ASSERT_LT (14, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_backup)
{
	auto dir (badem::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / "data.ldb";
	/** Returns 'dir' if backup file cannot be found */
	// clang-format off
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("data_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};
	// clang-format on

	{
		badem::logger_mt logger;
		badem::genesis genesis;
		badem::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 14);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	badem::logger_mt logger;
	badem::mdb_store store (logger, path, badem::txn_tracking_config{}, std::chrono::seconds (5), 128, 512, true);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (14, store.version_get (transaction));
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	auto path (badem::unique_path ());
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);

	badem::account account1 (0);
	badem::account account2 (1);
	badem::account account3 (2);
	{
		auto transaction (store.tx_begin_write ());
		store.confirmation_height_put (transaction, account1, 500);
		store.confirmation_height_put (transaction, account2, std::numeric_limits<uint64_t>::max ());
		store.confirmation_height_put (transaction, account3, 10);

		uint64_t confirmation_height;
		ASSERT_FALSE (store.confirmation_height_get (transaction, account1, confirmation_height));
		ASSERT_EQ (confirmation_height, 500);
		ASSERT_FALSE (store.confirmation_height_get (transaction, account2, confirmation_height));
		ASSERT_EQ (confirmation_height, std::numeric_limits<uint64_t>::max ());
		ASSERT_FALSE (store.confirmation_height_get (transaction, account3, confirmation_height));
		ASSERT_EQ (confirmation_height, 10);

		// Check cleaning of confirmation heights
		store.confirmation_height_clear (transaction);
	}
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.confirmation_height_count (transaction), 3);
	uint64_t confirmation_height;
	ASSERT_FALSE (store.confirmation_height_get (transaction, account1, confirmation_height));
	ASSERT_EQ (confirmation_height, 0);
	ASSERT_FALSE (store.confirmation_height_get (transaction, account2, confirmation_height));
	ASSERT_EQ (confirmation_height, 0);
	ASSERT_FALSE (store.confirmation_height_get (transaction, account3, confirmation_height));
	ASSERT_EQ (confirmation_height, 0);
}

// Upgrade many accounts and check they all have a confirmation height of 0 (except genesis which should have 1)
TEST (mdb_block_store, upgrade_confirmation_height_many)
{
	auto error (false);
	badem::genesis genesis;
	auto total_num_accounts = 1000; // Includes the genesis account

	auto path (badem::unique_path ());
	{
		badem::logger_mt logger;
		badem::mdb_store store (logger, path);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 13);
		badem::rep_weights rep_weights;
		std::atomic<uint64_t> cemented_count{ 0 };
		std::atomic<uint64_t> block_count_cache{ 0 };
		store.initialize (transaction, genesis, rep_weights, cemented_count, block_count_cache);
		modify_account_info_to_v13 (store, transaction, badem::genesis_account, genesis.open->hash ());

		// Add many accounts
		for (auto i = 0; i < total_num_accounts - 1; ++i)
		{
			badem::account account (i);
			badem::open_block open (1, badem::genesis_account, 3, nullptr);
			badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
			store.block_put (transaction, open.hash (), open, sideband);
			badem::account_info_v13 account_info_v13 (open.hash (), open.hash (), open.hash (), 3, 4, 1, badem::epoch::epoch_0);
			auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, badem::mdb_val (account), badem::mdb_val (account_info_v13), 0));
			ASSERT_EQ (status, 0);
		}

		ASSERT_EQ (store.count (transaction, store.accounts_v0), total_num_accounts);
	}

	// Loop over them all and confirm they all have the correct confirmation heights
	badem::logger_mt logger;
	badem::mdb_store store (logger, path);
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.account_count (transaction), total_num_accounts);
	ASSERT_EQ (store.confirmation_height_count (transaction), total_num_accounts);

	for (auto i (store.confirmation_height_begin (transaction)), n (store.confirmation_height_end ()); i != n; ++i)
	{
		ASSERT_EQ (i->second, (i->first == badem::genesis_account) ? 1 : 0);
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (badem::unique_path ());
	badem::logger_mt logger;
	{
		auto store = badem::make_store (logger, path);
		ASSERT_FALSE (store->init_error ());

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store->tx_begin_write ());
		store->version_put (transaction, std::numeric_limits<int>::max ());
	}

	// Now try and read it, should give an error
	{
		auto store = badem::make_store (logger, path, true);
		ASSERT_TRUE (store->init_error ());

		auto transaction = store->tx_begin_read ();
		auto version_l = store->version_get (transaction);
		ASSERT_EQ (version_l, std::numeric_limits<int>::max ());
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	badem::logger_mt logger;
	auto store = badem::make_store (logger, badem::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	badem::keypair key1;
	badem::open_block block (0, 1, 1, badem::keypair ().prv, 0, 0);
	auto hash1 (block.hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block_get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		badem::block_sideband sideband (badem::block_type::open, 0, 0, 0, 0, 0, badem::epoch::epoch_0);
		store->block_put (write_transaction, hash1, block, sideband);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block_get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, rocksdb_force_test_env_variable)
{
	badem::logger_mt logger;

	// Set environment variable
	constexpr auto env_var = "TEST_USE_ROCKSDB";
	auto value = std::getenv (env_var);
	(void)value;

	auto store = badem::make_store (logger, badem::unique_path ());

	auto mdb_cast = dynamic_cast<badem::mdb_store *> (store.get ());

#if BADEM_ROCKSDB
	if (value && boost::lexical_cast<int> (value) == 1)
	{
		ASSERT_NE (boost::polymorphic_downcast<badem::rocksdb_store *> (store.get ()), nullptr);
	}
	else
	{
		ASSERT_NE (mdb_cast, nullptr);
	}
#else
	ASSERT_NE (mdb_cast, nullptr);
#endif
}

namespace
{
void write_sideband_v12 (badem::mdb_store & store_a, badem::transaction & transaction_a, badem::block & block_a, badem::block_hash const & successor_a, MDB_dbi db_a)
{
	std::vector<uint8_t> vector;
	{
		badem::vectorstream stream (vector);
		block_a.serialize (stream);
		badem::write (stream, successor_a);
	}
	MDB_val val{ vector.size (), vector.data () };
	auto hash (block_a.hash ());
	auto status (mdb_put (store_a.env.tx (transaction_a), db_a, badem::mdb_val (hash), &val, 0));
	ASSERT_EQ (0, status);
	badem::block_sideband_v14 sideband_v14;
	auto block (store_a.block_get_v14 (transaction_a, hash, &sideband_v14));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (0, sideband_v14.height);
};

void write_sideband_v14 (badem::mdb_store & store_a, badem::transaction & transaction_a, badem::block const & block_a, MDB_dbi db_a)
{
	badem::block_sideband sideband;
	auto block = store_a.block_get (transaction_a, block_a.hash (), &sideband);
	ASSERT_NE (block, nullptr);

	badem::block_sideband_v14 sideband_v14 (sideband.type, sideband.account, sideband.successor, sideband.balance, sideband.timestamp, sideband.height);
	std::vector<uint8_t> data;
	{
		badem::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v14.serialize (stream);
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), sideband.epoch == badem::epoch::epoch_0 ? store_a.state_blocks_v0 : store_a.state_blocks_v1, badem::mdb_val (block_a.hash ()), &val, 0));
}

// These functions take the latest account_info and create a legacy one so that upgrade tests can be emulated more easily.
void modify_account_info_to_v13 (badem::mdb_store & store, badem::transaction const & transaction, badem::account const & account, badem::block_hash const & rep_block)
{
	badem::account_info info;
	ASSERT_FALSE (store.account_get (transaction, account, info));
	badem::account_info_v13 account_info_v13 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), (info.epoch () == badem::epoch::epoch_0) ? store.accounts_v0 : store.accounts_v1, badem::mdb_val (account), badem::mdb_val (account_info_v13), 0));
	(void)status;
	assert (status == 0);
}

void modify_account_info_to_v14 (badem::mdb_store & store, badem::transaction const & transaction, badem::account const & account, uint64_t confirmation_height, badem::block_hash const & rep_block)
{
	badem::account_info info;
	ASSERT_FALSE (store.account_get (transaction, account, info));
	badem::account_info_v14 account_info_v14 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, confirmation_height, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), info.epoch () == badem::epoch::epoch_0 ? store.accounts_v0 : store.accounts_v1, badem::mdb_val (account), badem::mdb_val (account_info_v14), 0));
	(void)status;
	assert (status == 0);
}

void modify_genesis_account_info_to_v5 (badem::mdb_store & store, badem::transaction const & transaction)
{
	badem::account_info info;
	store.account_get (transaction, badem::test_genesis_key.pub, info);
	badem::representative_visitor visitor (transaction, store);
	visitor.compute (info.head);
	badem::account_info_v5 info_old (info.head, visitor.result, info.open_block, info.balance, info.modified);
	auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, badem::mdb_val (badem::test_genesis_key.pub), badem::mdb_val (sizeof (info_old), &info_old), 0));
	(void)status;
	assert (status == 0);
}
}
