#include <badem/core_test/testutil.hpp>
#include <badem/lib/stats.hpp>
#include <badem/node/testing.hpp>

#include <crypto/cryptopp/filters.h>
#include <crypto/cryptopp/randpool.h>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, boost::filesystem::path ("///"));
	ASSERT_FALSE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::account account;
	auto transaction (store->tx_begin_read ());
	auto balance (ledger.account_balance (transaction, account));
	ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	auto balance (ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, balance);
	auto amount (ledger.amount (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, amount);
	badem::account_info info;
	ASSERT_FALSE (store->account_get (transaction, badem::genesis_account, info));
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (badem::seconds_since_epoch (), info.modified);
	ASSERT_LT (badem::seconds_since_epoch () - info.modified, 10);
	// Genesis block should be confirmed by default
	uint64_t confirmation_height;
	ASSERT_FALSE (store->confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (confirmation_height, 1);
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	badem::system system (24000, 2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin_read ());
		ASSERT_EQ (badem::genesis_amount, i->ledger.account_balance (transaction, badem::genesis_account));
	}
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	badem::genesis genesis;
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::keypair key2;
	badem::send_block send (info1.head, key2.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	badem::block_hash hash1 (send.hash ());
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_EQ (1, info1.block_count);
	// This was a valid block, it should progress.
	auto return1 (ledger.process (transaction, send));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.amount (transaction, hash1));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, hash1));
	ASSERT_EQ (badem::process_result::progress, return1.code);
	ASSERT_EQ (badem::test_genesis_key.pub, return1.account);
	ASSERT_EQ (badem::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (50, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	badem::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info2));
	ASSERT_EQ (2, info2.block_count);
	auto latest6 (store->block_get (transaction, info2.head));
	ASSERT_NE (nullptr, latest6);
	auto latest7 (dynamic_cast<badem::send_block *> (latest6.get ()));
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
	badem::open_block open (hash1, key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	badem::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
	auto return2 (ledger.process (transaction, open));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (badem::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (badem::genesis_amount - 50, return2.amount.number ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, key2.pub));
	badem::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info3));
	auto latest2 (store->block_get (transaction, info3.head));
	ASSERT_NE (nullptr, latest2);
	auto latest3 (dynamic_cast<badem::send_block *> (latest2.get ()));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (send, *latest3);
	badem::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info4));
	auto latest4 (store->block_get (transaction, info4.head));
	ASSERT_NE (nullptr, latest4);
	auto latest5 (dynamic_cast<badem::open_block *> (latest4.get ()));
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (open, *latest5);
	ASSERT_FALSE (ledger.rollback (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	badem::account_info info5;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info5));
	badem::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, badem::pending_key (key2.pub, hash1), pending1));
	ASSERT_EQ (badem::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (badem::genesis_amount - 50, pending1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (50, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	badem::account_info info6;
	ASSERT_FALSE (ledger.store.account_get (transaction, badem::test_genesis_key.pub, info6));
	ASSERT_EQ (hash1, info6.head);
	ASSERT_FALSE (ledger.rollback (transaction, info6.head));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, hash1).is_zero ());
	badem::account_info info7;
	ASSERT_FALSE (ledger.store.account_get (transaction, badem::test_genesis_key.pub, info7));
	ASSERT_EQ (1, info7.block_count);
	ASSERT_EQ (info1.head, info7.head);
	badem::pending_info pending2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, badem::pending_key (key2.pub, hash1), pending2));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
}

TEST (ledger, process_receive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::keypair key2;
	badem::send_block send (info1.head, key2.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	badem::block_hash hash1 (send.hash ());
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	badem::keypair key3;
	badem::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	badem::block_hash hash2 (open.hash ());
	auto return1 (ledger.process (transaction, open));
	ASSERT_EQ (badem::process_result::progress, return1.code);
	ASSERT_EQ (key2.pub, return1.account);
	ASSERT_EQ (badem::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	badem::send_block send2 (hash1, key2.pub, 25, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (hash1));
	badem::block_hash hash3 (send2.hash ());
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send2).code);
	badem::receive_block receive (hash2, hash3, key2.prv, key2.pub, pool.generate (hash2));
	auto hash4 (receive.hash ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	auto return2 (ledger.process (transaction, receive));
	ASSERT_EQ (25, ledger.amount (transaction, hash4));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash4));
	ASSERT_EQ (badem::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (25, return2.amount.number ());
	ASSERT_EQ (hash4, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 25, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash4));
	ASSERT_TRUE (store->block_successor (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash4).is_zero ());
	ASSERT_EQ (25, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (25, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	badem::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, badem::pending_key (key2.pub, hash3), pending1));
	ASSERT_EQ (badem::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (25, pending1.amount.number ());
}

TEST (ledger, rollback_receiver)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::keypair key2;
	badem::send_block send (info1.head, key2.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	badem::block_hash hash1 (send.hash ());
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	badem::keypair key3;
	badem::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	badem::block_hash hash2 (open.hash ());
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash1));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	badem::account_info info2;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info2));
	badem::pending_info pending1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, badem::pending_key (key2.pub, info2.head), pending1));
}

TEST (ledger, rollback_representation)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key5;
	badem::change_block change1 (genesis.hash (), key5.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change1).code);
	badem::keypair key3;
	badem::change_block change2 (change1.hash (), key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (change1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change2).code);
	badem::keypair key2;
	badem::send_block send1 (change2.hash (), key2.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (change2.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::keypair key4;
	badem::open_block open (send1.hash (), key4.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open).code);
	badem::send_block send2 (send1.hash (), key2.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send2).code);
	badem::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, pool.generate (open.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (badem::genesis_amount - 1, ledger.weight (transaction, key4.pub));
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info1));
	ASSERT_EQ (open.hash (), info1.rep_block);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	badem::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info2));
	ASSERT_EQ (open.hash (), info2.rep_block);
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, key4.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key4.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, key3.pub));
	badem::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info3));
	ASSERT_EQ (change2.hash (), info3.rep_block);
	ASSERT_FALSE (ledger.rollback (transaction, change2.hash ()));
	badem::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info4));
	ASSERT_EQ (change1.hash (), info4.rep_block);
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, key5.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
}

TEST (ledger, receive_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send (genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	badem::receive_block receive (send.hash (), send.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
}

TEST (ledger, process_duplicate)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::keypair key2;
	badem::send_block send (info1.head, key2.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	badem::block_hash hash1 (send.hash ());
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (badem::process_result::old, ledger.process (transaction, send).code);
	badem::open_block open (hash1, 1, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (badem::process_result::old, ledger.process (transaction, open).code);
}

TEST (ledger, representative_genesis)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	auto latest (ledger.latest (transaction, badem::test_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (genesis.open->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
}

TEST (ledger, representative_change)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key2;
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::change_block block (info1.head, key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (0, ledger.amount (transaction, block.hash ()));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, block.hash ()));
	ASSERT_EQ (badem::process_result::progress, return1.code);
	ASSERT_EQ (badem::test_genesis_key.pub, return1.account);
	ASSERT_EQ (0, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, key2.pub));
	badem::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info2));
	ASSERT_EQ (block.hash (), info2.head);
	ASSERT_FALSE (ledger.rollback (transaction, info2.head));
	ASSERT_EQ (badem::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, block.hash ()).is_zero ());
	badem::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info3));
	ASSERT_EQ (info1.head, info3.head);
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
}

TEST (ledger, send_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key2;
	badem::keypair key3;
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::send_block block (info1.head, key2.pub, 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block).code);
	badem::send_block block2 (info1.head, key3.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, receive_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key2;
	badem::keypair key3;
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::send_block block (info1.head, key2.pub, 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block).code);
	badem::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	badem::change_block block3 (block2.hash (), key3.pub, key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block3).code);
	badem::send_block block4 (block.hash (), key2.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block4).code);
	badem::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, block5).code);
}

TEST (ledger, open_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key2;
	badem::keypair key3;
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::send_block block (info1.head, key2.pub, 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block).code);
	badem::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	badem::open_block block3 (block.hash (), key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, block3).code);
}

TEST (system, DISABLED_generate_send_existing)
{
	badem::system system (24000, 1);
	badem::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (badem::test_genesis_key.prv);
	badem::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (badem::genesis_account, stake_preserver.pub, badem::genesis_amount / 3 * 2, true));
	badem::account_info info1;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, badem::test_genesis_key.pub, info1));
	}
	std::vector<badem::account> accounts;
	accounts.push_back (badem::test_genesis_key.pub);
	system.generate_send_existing (*system.nodes[0], accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		auto open_block (std::make_shared<badem::open_block> (send_block->hash (), badem::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (badem::genesis_account));
	badem::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, badem::test_genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, badem::test_genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, badem::genesis_amount / 3);
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_NE (system.nodes[0]->ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, generate_send_new)
{
	badem::system system (24000, 1);
	badem::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (badem::test_genesis_key.prv);
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		auto iterator1 (system.nodes[0]->store.latest_begin (transaction));
		ASSERT_NE (system.nodes[0]->store.latest_end (), iterator1);
		++iterator1;
		ASSERT_EQ (system.nodes[0]->store.latest_end (), iterator1);
	}
	badem::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (badem::genesis_account, stake_preserver.pub, badem::genesis_amount / 3 * 2, true));
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		auto open_block (std::make_shared<badem::open_block> (send_block->hash (), badem::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (badem::genesis_account));
	std::vector<badem::account> accounts;
	accounts.push_back (badem::test_genesis_key.pub);
	system.generate_send_new (*system.nodes[0], accounts);
	badem::account new_account (0);
	{
		auto transaction (system.nodes[0]->wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (badem::uint256_union (iterator2->first) != badem::test_genesis_key.pub)
		{
			new_account = badem::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (badem::uint256_union (iterator2->first) != badem::test_genesis_key.pub)
		{
			new_account = badem::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	system.deadline_set (10s);
	while (system.nodes[0]->balance (new_account) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.stop ();
	runner.join ();
}

TEST (ledger, representation)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (badem::genesis_amount, store->representation_get (transaction, badem::test_genesis_key.pub));
	badem::keypair key2;
	badem::send_block block1 (genesis.hash (), key2.pub, badem::genesis_amount - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (badem::genesis_amount - 100, store->representation_get (transaction, badem::test_genesis_key.pub));
	badem::keypair key3;
	badem::open_block block2 (block1.hash (), key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (badem::genesis_amount - 100, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key3.pub));
	badem::send_block block3 (block1.hash (), key2.pub, badem::genesis_amount - 200, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key3.pub));
	badem::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (200, store->representation_get (transaction, key3.pub));
	badem::keypair key4;
	badem::change_block block5 (block4.hash (), key4.pub, key2.prv, key2.pub, pool.generate (block4.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key3.pub));
	ASSERT_EQ (200, store->representation_get (transaction, key4.pub));
	badem::keypair key5;
	badem::send_block block6 (block5.hash (), key5.pub, 100, key2.prv, key2.pub, pool.generate (block5.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key5.pub));
	badem::keypair key6;
	badem::open_block block7 (block6.hash (), key6.pub, key5.pub, key5.prv, key5.pub, pool.generate (key5.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block7).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key6.pub));
	badem::send_block block8 (block6.hash (), key5.pub, 0, key2.prv, key2.pub, pool.generate (block6.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block8).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store->representation_get (transaction, key6.pub));
	badem::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, pool.generate (block7.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block9).code);
	ASSERT_EQ (badem::genesis_amount - 200, store->representation_get (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store->representation_get (transaction, key5.pub));
	ASSERT_EQ (200, store->representation_get (transaction, key6.pub));
}

TEST (ledger, double_open)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key2;
	badem::send_block send1 (genesis.hash (), key2.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::open_block open2 (send1.hash (), badem::test_genesis_key.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, open2).code);
}

TEST (ledger, double_receive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key2;
	badem::send_block send1 (genesis.hash (), key2.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (badem::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (votes, check_signature)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.online_weight_minimum = std::numeric_limits<badem::uint128_t>::max ();
	auto & node1 = *system.add_node (node_config);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, badem::genesis_amount - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	node1.active.start (send1);
	std::lock_guard<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send1));
	vote1->signature.bytes[0] ^= 1;
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_EQ (badem::vote_code::invalid, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, badem::endpoint (boost::asio::ip::address_v6 (), 0))));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (badem::vote_code::vote, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, badem::endpoint (boost::asio::ip::address_v6 (), 0))));
	ASSERT_EQ (badem::vote_code::replay, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, badem::endpoint (boost::asio::ip::address_v6 (), 0))));
}

TEST (votes, add_one)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, badem::genesis_amount - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	auto vote2 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 2, send1));
	ASSERT_FALSE (node1.active.vote (vote2));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes.size ());
	auto existing1 (votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_NE (votes1->last_votes.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (badem::genesis_amount - 100, winner.first);
}

TEST (votes, add_two)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, badem::genesis_amount - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	lock.unlock ();
	badem::keypair key2;
	auto send2 (std::make_shared<badem::send_block> (genesis.hash (), key2.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<badem::vote> (key2.pub, key2.prv, 1, send2));
	ASSERT_FALSE (node1.active.vote (vote2));
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	lock.lock ();
	ASSERT_EQ (3, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[badem::test_genesis_key.pub].hash);
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (key2.pub));
	ASSERT_EQ (send2->hash (), votes1->last_votes[key2.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Higher sequence numbers change the vote
TEST (votes, add_existing)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.online_weight_minimum = std::numeric_limits<badem::uint128_t>::max ();
	badem::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	node1.active.start (send1);
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	// Block is already processed from vote
	ASSERT_TRUE (node1.active.publish (send1));
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes[badem::test_genesis_key.pub].sequence);
	badem::keypair key2;
	auto send2 (std::make_shared<badem::send_block> (genesis.hash (), key2.pub, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 2, send2));
	// Pretend we've waited the timeout
	votes1->last_votes[badem::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_FALSE (node1.active.vote (vote2));
	ASSERT_FALSE (node1.active.publish (send2));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes[badem::test_genesis_key.pub].sequence);
	// Also resend the old vote, and see if we respect the sequence number
	votes1->last_votes[badem::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_TRUE (node1.active.vote (vote1));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes[badem::test_genesis_key.pub].sequence);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_EQ (send2->hash (), votes1->last_votes[badem::test_genesis_key.pub].hash);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto winner (*votes1->tally (transaction).begin ());
		ASSERT_EQ (*send2, *winner.second);
	}
}

// Lower sequence numbers are ignored
TEST (votes, add_old)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 2, send1));
	std::lock_guard<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto channel (std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	node1.vote_processor.vote_blocking (transaction, vote1, channel);
	badem::keypair key2;
	auto send2 (std::make_shared<badem::send_block> (genesis.hash (), key2.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send2));
	votes1->last_votes[badem::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	node1.vote_processor.vote_blocking (transaction, vote2, channel);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[badem::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Lower sequence numbers are accepted for different accounts
TEST (votes, add_old_different_account)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<badem::send_block> (send1->hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send2).code);
	node1.active.start (send1);
	node1.active.start (send2);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto votes2 (node1.active.roots.find (send2->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	auto vote_result1 (node1.vote_processor.vote_blocking (transaction, vote1, channel));
	ASSERT_EQ (badem::vote_code::vote, vote_result1);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote2 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send2));
	auto vote_result2 (node1.vote_processor.vote_blocking (transaction, vote2, channel));
	ASSERT_EQ (badem::vote_code::vote, vote_result2);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (2, votes2->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_NE (votes2->last_votes.end (), votes2->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[badem::test_genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), votes2->last_votes[badem::test_genesis_key.pub].hash);
	auto winner1 (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner1.second);
	auto winner2 (*votes2->tally (transaction).begin ());
	ASSERT_EQ (*send2, *winner2.second);
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	auto send1 (std::make_shared<badem::send_block> (genesis.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto vote1 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 1, send1));
	auto channel (std::make_shared<badem::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	node1.vote_processor.vote_blocking (transaction, vote1, channel);
	badem::keypair key2;
	auto send2 (std::make_shared<badem::send_block> (genesis.hash (), key2.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<badem::vote> (badem::test_genesis_key.pub, badem::test_genesis_key.prv, 2, send2));
	node1.vote_processor.vote_blocking (transaction, vote2, channel);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (badem::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[badem::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Query for block successor
TEST (ledger, successor)
{
	badem::system system (24000, 1);
	badem::keypair key1;
	badem::genesis genesis;
	badem::send_block send1 (genesis.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0);
	system.nodes[0]->work_generate_blocking (send1);
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, send1).code);
	ASSERT_EQ (send1, *system.nodes[0]->ledger.successor (transaction, badem::qualified_root (genesis.hash (), 0)));
	ASSERT_EQ (*genesis.open, *system.nodes[0]->ledger.successor (transaction, genesis.open->qualified_root ()));
	ASSERT_EQ (nullptr, system.nodes[0]->ledger.successor (transaction, badem::qualified_root (0)));
}

TEST (ledger, fail_change_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::change_block block (genesis.hash (), key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::old, result2.code);
}

TEST (ledger, fail_change_gap_previous)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::change_block block (1, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (1));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_change_bad_signature)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::change_block block (genesis.hash (), key1.pub, badem::keypair ().prv, 0, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_change_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::change_block block1 (genesis.hash (), key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::keypair key2;
	badem::change_block block2 (genesis.hash (), key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::fork, result2.code);
}

TEST (ledger, fail_send_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::old, result2.code);
}

TEST (ledger, fail_send_gap_previous)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block (1, key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (1));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_send_bad_signature)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block (genesis.hash (), key1.pub, 1, badem::keypair ().prv, 0, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (badem::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_send_negative_spend)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::keypair key2;
	badem::send_block block2 (block1.hash (), key2.pub, 2, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (badem::process_result::negative_spend, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_send_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::keypair key2;
	badem::send_block block2 (genesis.hash (), key2.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (badem::process_result::old, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_gap_source)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::open_block block2 (1, 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::gap_source, result2.code);
}

TEST (ledger, fail_open_bad_signature)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	block2.signature.clear ();
	ASSERT_EQ (badem::process_result::bad_signature, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_fork_previous)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block3).code);
	badem::open_block block4 (block2.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_open_account_mismatch)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::keypair badkey;
	badem::open_block block2 (block1.hash (), 1, badkey.pub, badkey.prv, badkey.pub, pool.generate (badkey.pub));
	ASSERT_NE (badem::process_result::progress, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_receive_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block3).code);
	badem::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (badem::process_result::old, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_receive_gap_source)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (badem::process_result::gap_source, result4.code);
}

TEST (ledger, fail_receive_overreceive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, pool.generate (block2.hash ()));
	auto result4 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::unreceivable, result4.code);
}

TEST (ledger, fail_receive_bad_signature)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::receive_block block4 (block3.hash (), block2.hash (), badem::keypair ().prv, 0, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (badem::process_result::bad_signature, result4.code);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, pool.generate (1));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (badem::process_result::gap_previous, result4.code);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, pool.generate (1));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::gap_previous, result3.code);
}

TEST (ledger, fail_receive_fork_previous)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::keypair key2;
	badem::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (badem::process_result::progress, result4.code);
	badem::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (badem::process_result::fork, result5.code);
}

TEST (ledger, fail_receive_received_source)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key1;
	badem::send_block block1 (genesis.hash (), key1.pub, 2, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (badem::process_result::progress, result1.code);
	badem::send_block block2 (block1.hash (), key1.pub, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (badem::process_result::progress, result2.code);
	badem::send_block block6 (block2.hash (), key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block2.hash ()));
	auto result6 (ledger.process (transaction, block6));
	ASSERT_EQ (badem::process_result::progress, result6.code);
	badem::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (badem::process_result::progress, result3.code);
	badem::keypair key2;
	badem::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (badem::process_result::progress, result4.code);
	badem::receive_block block5 (block4.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block4.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (badem::process_result::progress, result5.code);
	badem::receive_block block7 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result7 (ledger.process (transaction, block7));
	ASSERT_EQ (badem::process_result::fork, result7.code);
}

TEST (ledger, latest_empty)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key;
	auto transaction (store->tx_begin_read ());
	auto latest (ledger.latest (transaction, key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_FALSE (init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub));
	auto hash1 (ledger.latest (transaction, badem::test_genesis_key.pub));
	badem::send_block send (hash1, 0, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (hash1));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (send.hash (), ledger.latest_root (transaction, badem::test_genesis_key.pub));
}

TEST (ledger, change_representative_move_representation)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::keypair key1;
	auto transaction (store->tx_begin_write ());
	badem::genesis genesis;
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto hash1 (genesis.hash ());
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::test_genesis_key.pub));
	badem::send_block send (hash1, key1.pub, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (hash1));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (0, ledger.weight (transaction, badem::test_genesis_key.pub));
	badem::keypair key2;
	badem::change_block change (send.hash (), key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change).code);
	badem::keypair key3;
	badem::open_block open (send.hash (), key3.pub, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	badem::genesis genesis;
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
	badem::keypair key1;
	badem::send_block send1 (info1.head, key1.pub, badem::genesis_amount - 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
	auto return1 (ledger.process (transaction, send1));
	ASSERT_EQ (badem::process_result::progress, return1.code);
	badem::send_block send2 (send1.hash (), key1.pub, badem::genesis_amount - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	auto return2 (ledger.process (transaction, send2));
	ASSERT_EQ (badem::process_result::progress, return2.code);
	badem::keypair key2;
	badem::open_block open (send2.hash (), key2.pub, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto return4 (ledger.process (transaction, open));
	ASSERT_EQ (badem::process_result::progress, return4.code);
	badem::receive_block receive (open.hash (), send1.hash (), key1.prv, key1.pub, pool.generate (open.hash ()));
	auto return5 (ledger.process (transaction, receive));
	ASSERT_EQ (badem::process_result::progress, return5.code);
	badem::keypair key3;
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (badem::genesis_amount - 100, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	badem::change_block change1 (send2.hash (), key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send2.hash ()));
	auto return6 (ledger.process (transaction, change1));
	ASSERT_EQ (badem::process_result::progress, return6.code);
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
	ASSERT_EQ (50, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_EQ (badem::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (badem::genesis_amount - 100, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send2.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (badem::genesis_amount - 50, ledger.weight (transaction, badem::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (badem::genesis_amount - 0, ledger.weight (transaction, badem::test_genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::account_info info1;
	badem::keypair key2;
	badem::genesis genesis;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis);
		ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
		badem::send_block send (info1.head, key2.pub, std::numeric_limits<badem::uint128_t>::max () - 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
		ledger.process (transaction, send);
	}
	{
		auto transaction (store->tx_begin_read ());
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (transaction, key2.pub));
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, info1));
		badem::send_block send (info1.head, key2.pub, std::numeric_limits<badem::uint128_t>::max () - 100, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (info1.head));
		ledger.process (transaction, send);
	}
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	}
}

TEST (ledger, block_destination_source)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair dest;
	badem::uint128_t balance (badem::genesis_amount);
	balance -= badem::kBDM_ratio;
	badem::send_block block1 (genesis.hash (), dest.pub, balance, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	balance -= badem::kBDM_ratio;
	badem::send_block block2 (block1.hash (), badem::genesis_account, balance, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block1.hash ()));
	balance += badem::kBDM_ratio;
	badem::receive_block block3 (block2.hash (), block2.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block2.hash ()));
	balance -= badem::kBDM_ratio;
	badem::state_block block4 (badem::genesis_account, block3.hash (), badem::genesis_account, balance, dest.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block3.hash ()));
	balance -= badem::kBDM_ratio;
	badem::state_block block5 (badem::genesis_account, block4.hash (), badem::genesis_account, balance, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block4.hash ()));
	balance += badem::kBDM_ratio;
	badem::state_block block6 (badem::genesis_account, block5.hash (), badem::genesis_account, balance, block5.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (block5.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (balance, ledger.balance (transaction, block6.hash ()));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block1));
	ASSERT_TRUE (ledger.block_source (transaction, block1).is_zero ());
	ASSERT_EQ (badem::genesis_account, ledger.block_destination (transaction, block2));
	ASSERT_TRUE (ledger.block_source (transaction, block2).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block3).is_zero ());
	ASSERT_EQ (block2.hash (), ledger.block_source (transaction, block3));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block4));
	ASSERT_TRUE (ledger.block_source (transaction, block4).is_zero ());
	ASSERT_EQ (badem::genesis_account, ledger.block_destination (transaction, block5));
	ASSERT_TRUE (ledger.block_source (transaction, block5).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block6).is_zero ());
	ASSERT_EQ (block5.hash (), ledger.block_source (transaction, block6));
}

TEST (ledger, state_account)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (badem::genesis_account, ledger.account (transaction, send1.hash ()));
}

TEST (ledger, state_send_receive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, send1.hash ())));
	badem::state_block receive1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (badem::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, send1.hash ())));
}

TEST (ledger, state_receive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send1 (genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::state_block receive1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (badem::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
}

TEST (ledger, state_rep_change)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair rep;
	badem::state_block change1 (badem::genesis_account, genesis.hash (), rep.pub, badem::genesis_amount, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (store->block_exists (transaction, change1.hash ()));
	auto change2 (store->block_get (transaction, change1.hash ()));
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (change1, *change2);
	ASSERT_EQ (badem::genesis_amount, ledger.balance (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.amount (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, badem::pending_key (destination.pub, send1.hash ())));
	badem::state_block open1 (destination.pub, 0, badem::genesis_account, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (destination.pub, send1.hash ())));
	ASSERT_TRUE (store->block_exists (transaction, open1.hash ()));
	auto open2 (store->block_get (transaction, open1.hash ()));
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (open1, *open2);
	ASSERT_EQ (badem::kBDM_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::send_block send2 (send1.hash (), badem::genesis_account, badem::genesis_amount - (2 * badem::kBDM_ratio), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, send2).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::receive_block receive1 (send1.hash (), send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, receive1).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::keypair rep;
	badem::change_block change1 (send1.hash (), rep.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, change1).code);
}

TEST (ledger, state_unreceivable_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send1 (genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::state_block receive1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount, 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::gap_source, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_receive_bad_amount_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send1 (genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::state_block receive1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::balance_mismatch, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_no_link_amount_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::keypair rep;
	badem::state_block change1 (badem::genesis_account, send1.hash (), rep.pub, badem::genesis_amount, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::balance_mismatch, ledger.process (transaction, change1).code);
}

TEST (ledger, state_receive_wrong_account_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::keypair key;
	badem::state_block receive1 (key.pub, 0, badem::genesis_account, badem::kBDM_ratio, send1.hash (), key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (badem::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_open_state_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block open1 (destination.pub, 0, badem::genesis_account, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::open_block open2 (send1.hash (), badem::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_state_open_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::open_block open1 (send1.hash (), badem::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::state_block open2 (destination.pub, 0, badem::genesis_account, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_open_previous_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block open1 (destination.pub, destination.pub, badem::genesis_account, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::gap_previous, ledger.process (transaction, open1).code);
}

TEST (ledger, state_open_source_fail)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block open1 (destination.pub, 0, badem::genesis_account, 0, 0, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::gap_source, ledger.process (transaction, open1).code);
}

TEST (ledger, state_send_change)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair rep;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), rep.pub, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::keypair rep;
	badem::state_block receive1 (badem::genesis_account, send1.hash (), rep.pub, badem::genesis_amount, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (badem::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::open_block open1 (send1.hash (), badem::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (badem::kBDM_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
}

TEST (ledger, state_receive_old)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block send2 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount - (2 * badem::kBDM_ratio), destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send2).code);
	badem::open_block open1 (send1.hash (), badem::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (2 * badem::kBDM_ratio, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
}

TEST (ledger, state_rollback_send)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, badem::pending_key (badem::genesis_account, send1.hash ()), info));
	ASSERT_EQ (badem::genesis_account, info.source);
	ASSERT_EQ (badem::kBDM_ratio, info.amount.number ());
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, send1.hash ())));
	ASSERT_TRUE (store->block_successor (transaction, genesis.hash ()).is_zero ());
}

TEST (ledger, state_rollback_receive)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block receive1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	badem::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, badem::pending_key (badem::genesis_account, send1.hash ()), info));
	ASSERT_EQ (badem::genesis_account, info.source);
	ASSERT_EQ (badem::kBDM_ratio, info.amount.number ());
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
}

TEST (ledger, state_rollback_received_send)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair key;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block receive1 (key.pub, 0, key.pub, badem::kBDM_ratio, send1.hash (), key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->pending_exists (transaction, badem::pending_key (badem::genesis_account, send1.hash ())));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (0, ledger.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key.pub));
}

TEST (ledger, state_rep_change_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair rep;
	badem::state_block change1 (badem::genesis_account, genesis.hash (), rep.pub, badem::genesis_amount, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, change1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block open1 (destination.pub, 0, badem::genesis_account, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (ledger.rollback (transaction, open1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, open1.hash ()));
	ASSERT_EQ (0, ledger.account_balance (transaction, destination.pub));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	badem::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, badem::pending_key (destination.pub, send1.hash ()), info));
	ASSERT_EQ (badem::genesis_account, info.source);
	ASSERT_EQ (badem::kBDM_ratio, info.amount.number ());
}

TEST (ledger, state_send_change_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair rep;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), rep.pub, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (badem::genesis_amount, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change_rollback)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::keypair rep;
	badem::state_block receive1 (badem::genesis_account, send1.hash (), rep.pub, badem::genesis_amount, send1.hash (), badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.account_balance (transaction, badem::genesis_account));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, epoch_blocks_general)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::keypair epoch_key;
	badem::ledger ledger (*store, stats, 123, epoch_key.pub);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block epoch1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch1).code);
	badem::state_block epoch2 (badem::genesis_account, epoch1.hash (), badem::genesis_account, badem::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, epoch2).code);
	badem::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, badem::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, badem::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, badem::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, badem::epoch::epoch_0);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, badem::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, badem::epoch::epoch_1);
	badem::change_block change1 (epoch1.hash (), badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, change1).code);
	badem::state_block send1 (badem::genesis_account, epoch1.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::open_block open1 (send1.hash (), badem::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::unreceivable, ledger.process (transaction, open1).code);
	badem::state_block epoch3 (destination.pub, 0, badem::genesis_account, 0, 123, epoch_key.prv, epoch_key.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::representative_mismatch, ledger.process (transaction, epoch3).code);
	badem::state_block epoch4 (destination.pub, 0, 0, 0, 123, epoch_key.prv, epoch_key.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch4).code);
	badem::receive_block receive1 (epoch4.hash (), send1.hash (), destination.prv, destination.pub, pool.generate (epoch4.hash ()));
	ASSERT_EQ (badem::process_result::block_position, ledger.process (transaction, receive1).code);
	badem::state_block receive2 (destination.pub, epoch4.hash (), destination.pub, badem::kBDM_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (epoch4.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (0, ledger.balance (transaction, epoch4.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (badem::kBDM_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (badem::genesis_amount - badem::kBDM_ratio, ledger.weight (transaction, badem::genesis_account));
	ASSERT_EQ (badem::kBDM_ratio, ledger.weight (transaction, destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::keypair epoch_key;
	badem::ledger ledger (*store, stats, 123, epoch_key.pub);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::state_block send1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block epoch1 (badem::genesis_account, send1.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, 123, epoch_key.prv, epoch_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch1).code);
	badem::state_block send2 (badem::genesis_account, epoch1.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio * 2, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send2).code);
	badem::open_block open1 (send1.hash (), destination.pub, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	badem::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (badem::process_result::unreceivable, ledger.process (transaction, receive1).code);
	badem::state_block receive2 (destination.pub, open1.hash (), destination.pub, badem::kBDM_ratio * 2, send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive2).code);
	badem::account_info destination_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, badem::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, receive2.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, badem::epoch::epoch_0);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, badem::epoch::epoch_1);
	badem::keypair destination2;
	badem::state_block send3 (destination.pub, receive2.hash (), destination.pub, badem::kBDM_ratio, destination2.pub, destination.prv, destination.pub, pool.generate (receive2.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send3).code);
	badem::open_block open2 (send3.hash (), destination2.pub, destination2.pub, destination2.prv, destination2.pub, pool.generate (destination2.pub));
	ASSERT_EQ (badem::process_result::unreceivable, ledger.process (transaction, open2).code);
}

TEST (ledger, epoch_blocks_fork)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::keypair epoch_key;
	badem::ledger ledger (*store, stats, 123, epoch_key.pub);
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	badem::send_block send1 (genesis.hash (), badem::account (0), badem::genesis_amount, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	badem::state_block epoch1 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::fork, ledger.process (transaction, epoch1).code);
}

TEST (ledger, successor_epoch)
{
	badem::system system (24000, 1);
	badem::keypair key1;
	badem::genesis genesis;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send1 (genesis.hash (), key1.pub, badem::genesis_amount - 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	badem::state_block open (key1.pub, 0, key1.pub, 1, send1.hash (), key1.prv, key1.pub, pool.generate (key1.pub));
	badem::state_block change (key1.pub, open.hash (), key1.pub, 1, 0, key1.prv, key1.pub, pool.generate (open.hash ()));
	badem::state_block epoch_open (open.hash (), 0, 0, 0, system.nodes[0]->ledger.epoch_link, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (open.hash ()));
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, send1).code);
	ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, open).code);
	ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, change).code);
	ASSERT_EQ (badem::process_result::progress, system.nodes[0]->ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (change, *system.nodes[0]->ledger.successor (transaction, change.qualified_root ()));
	ASSERT_EQ (epoch_open, *system.nodes[0]->ledger.successor (transaction, epoch_open.qualified_root ()));
}

TEST (ledger, block_hash_account_conflict)
{
	badem::block_builder builder;
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair key1;
	badem::keypair key2;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());

	/*
	 * Generate a send block whose destination is a block hash already
	 * in the ledger and not an account
	 */
	std::shared_ptr<badem::state_block> send1 = builder.state ()
	                                           .account (badem::genesis_account)
	                                           .previous (genesis.hash ())
	                                           .representative (badem::genesis_account)
	                                           .balance (badem::genesis_amount - 100)
	                                           .link (key1.pub)
	                                           .sign (badem::test_genesis_key.prv, badem::test_genesis_key.pub)
	                                           .work (pool.generate (genesis.hash ()))
	                                           .build ();

	std::shared_ptr<badem::state_block> receive1 = builder.state ()
	                                              .account (key1.pub)
	                                              .previous (0)
	                                              .representative (badem::genesis_account)
	                                              .balance (100)
	                                              .link (send1->hash ())
	                                              .sign (key1.prv, key1.pub)
	                                              .work (pool.generate (key1.pub))
	                                              .build ();

	/*
	 * Note that the below link is a block hash when this is intended
	 * to represent a send state block. This can generally never be
	 * received , except by epoch blocks, which can sign an open block
	 * for arbitrary accounts.
	 */
	std::shared_ptr<badem::state_block> send2 = builder.state ()
	                                           .account (key1.pub)
	                                           .previous (receive1->hash ())
	                                           .representative (badem::genesis_account)
	                                           .balance (90)
	                                           .link (receive1->hash ())
	                                           .sign (key1.prv, key1.pub)
	                                           .work (pool.generate (receive1->hash ()))
	                                           .build ();

	/*
	 * Generate an epoch open for the account with the same value as the block hash
	 */
	std::shared_ptr<badem::state_block> open_epoch1 = builder.state ()
	                                                 .account (receive1->hash ())
	                                                 .previous (0)
	                                                 .representative (0)
	                                                 .balance (0)
	                                                 .link (node1.ledger.epoch_link)
	                                                 .sign (badem::test_genesis_key.prv, badem::test_genesis_key.pub)
	                                                 .work (pool.generate (receive1->hash ()))
	                                                 .build ();

	node1.work_generate_blocking (*send1);
	node1.work_generate_blocking (*receive1);
	node1.work_generate_blocking (*send2);
	node1.work_generate_blocking (*open_epoch1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send1).code);
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *receive1).code);
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *send2).code);
	ASSERT_EQ (badem::process_result::progress, node1.ledger.process (transaction, *open_epoch1).code);
	node1.active.start (send1);
	node1.active.start (receive1);
	node1.active.start (send2);
	node1.active.start (open_epoch1);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto votes2 (node1.active.roots.find (receive1->qualified_root ())->election);
	auto votes3 (node1.active.roots.find (send2->qualified_root ())->election);
	auto votes4 (node1.active.roots.find (open_epoch1->qualified_root ())->election);
	auto winner1 (*votes1->tally (transaction).begin ());
	auto winner2 (*votes2->tally (transaction).begin ());
	auto winner3 (*votes3->tally (transaction).begin ());
	auto winner4 (*votes4->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner1.second);
	ASSERT_EQ (*receive1, *winner2.second);
	ASSERT_EQ (*send2, *winner3.second);
	ASSERT_EQ (*open_epoch1, *winner4.second);
}

TEST (ledger, could_fit)
{
	badem::logger_mt logger;
	bool init (false);
	auto store = badem::make_store (init, logger, badem::unique_path ());
	ASSERT_TRUE (!init);
	badem::stat stats;
	badem::keypair epoch_key;
	badem::ledger ledger (*store, stats, 123, epoch_key.pub);
	badem::keypair epoch_signer;
	ledger.epoch_link = 123;
	ledger.epoch_signer = epoch_signer.pub;
	badem::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::keypair destination;
	// Test legacy and state change blocks could_fit
	badem::change_block change1 (genesis.hash (), badem::genesis_account, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	badem::state_block change2 (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount, 0, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	// Test legacy and state send
	badem::keypair key1;
	badem::send_block send1 (change1.hash (), key1.pub, badem::genesis_amount - 1, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (change1.hash ()));
	badem::state_block send2 (badem::genesis_account, change1.hash (), badem::genesis_account, badem::genesis_amount - 1, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (change1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, send1));
	ASSERT_FALSE (ledger.could_fit (transaction, send2));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	// Test legacy and state open
	badem::open_block open1 (send2.hash (), badem::genesis_account, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	badem::state_block open2 (key1.pub, 0, badem::genesis_account, 1, send2.hash (), key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_FALSE (ledger.could_fit (transaction, open1));
	ASSERT_FALSE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	// Create another send to receive
	badem::state_block send3 (badem::genesis_account, send2.hash (), badem::genesis_account, badem::genesis_amount - 2, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (send2.hash ()));
	// Test legacy and state receive
	badem::receive_block receive1 (open1.hash (), send3.hash (), key1.prv, key1.pub, pool.generate (open1.hash ()));
	badem::state_block receive2 (key1.pub, open1.hash (), badem::genesis_account, 2, send3.hash (), key1.prv, key1.pub, pool.generate (open1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, receive1));
	ASSERT_FALSE (ledger.could_fit (transaction, receive2));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	// Test epoch (state)
	badem::state_block epoch1 (key1.pub, receive1.hash (), badem::genesis_account, 2, ledger.epoch_link, epoch_signer.prv, epoch_signer.pub, pool.generate (receive1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
}

TEST (ledger, unchecked_epoch)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair destination;
	auto send1 (std::make_shared<badem::state_block> (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<badem::state_block> (destination.pub, 0, destination.pub, badem::kBDM_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto epoch1 (std::make_shared<badem::state_block> (destination.pub, open1->hash (), destination.pub, badem::kBDM_ratio, node1.ledger.epoch_link, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*epoch1);
	node1.block_processor.add (epoch1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, badem::signature_verification::valid_epoch);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		badem::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_EQ (info.epoch, badem::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_epoch_invalid)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair destination;
	auto send1 (std::make_shared<badem::state_block> (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<badem::state_block> (destination.pub, 0, destination.pub, badem::kBDM_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Epoch block with account own signature
	auto epoch1 (std::make_shared<badem::state_block> (destination.pub, open1->hash (), destination.pub, badem::kBDM_ratio, node1.ledger.epoch_link, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch1);
	// Pseudo epoch block (send subtype, destination - epoch link)
	auto epoch2 (std::make_shared<badem::state_block> (destination.pub, open1->hash (), destination.pub, badem::kBDM_ratio - 1, node1.ledger.epoch_link, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch2);
	node1.block_processor.add (epoch1);
	node1.block_processor.add (epoch2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 2);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 2);
		ASSERT_EQ (blocks[0].verified, badem::signature_verification::valid);
		ASSERT_EQ (blocks[1].verified, badem::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.block_exists (transaction, epoch1->hash ()));
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch2->hash ()));
		ASSERT_TRUE (node1.active.empty ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		badem::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_NE (info.epoch, badem::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_open)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair destination;
	auto send1 (std::make_shared<badem::state_block> (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<badem::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Invalid signature for open block
	auto open2 (std::make_shared<badem::open_block> (send1->hash (), badem::test_genesis_key.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open2);
	open2->signature.bytes[0] ^= 1;
	node1.block_processor.add (open1);
	node1.block_processor.add (open2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, open1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, badem::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, open1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
	}
}

TEST (ledger, unchecked_receive)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::genesis genesis;
	badem::keypair destination;
	auto send1 (std::make_shared<badem::state_block> (badem::genesis_account, genesis.hash (), badem::genesis_account, badem::genesis_amount - badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<badem::state_block> (badem::genesis_account, send1->hash (), badem::genesis_account, badem::genesis_amount - 2 * badem::kBDM_ratio, destination.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto open1 (std::make_shared<badem::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto receive1 (std::make_shared<badem::receive_block> (open1->hash (), send2->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*receive1);
	node1.block_processor.add (send1);
	node1.block_processor.add (receive1);
	node1.block_processor.flush ();
	// Previous block for receive1 is unknown, signature cannot be validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, receive1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, badem::signature_verification::unknown);
	}
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	// Previous block for receive1 is known, signature was validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, receive1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, badem::signature_verification::valid);
	}
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
	}
}

TEST (ledger, confirmation_height_not_updated)
{
	bool error (false);
	badem::logger_mt logger;
	auto store = badem::make_store (error, logger, badem::unique_path ());
	ASSERT_TRUE (!error);
	badem::stat stats;
	badem::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	badem::genesis genesis;
	store->initialize (transaction, genesis);
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::account_info account_info;
	ASSERT_FALSE (store->account_get (transaction, badem::test_genesis_key.pub, account_info));
	badem::keypair key;
	badem::send_block send1 (account_info.head, key.pub, 50, badem::test_genesis_key.prv, badem::test_genesis_key.pub, pool.generate (account_info.head));
	uint64_t confirmation_height;
	ASSERT_FALSE (store->confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (1, confirmation_height);
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (store->confirmation_height_get (transaction, badem::genesis_account, confirmation_height));
	ASSERT_EQ (1, confirmation_height);
	badem::open_block open1 (send1.hash (), badem::genesis_account, key.pub, key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (badem::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->confirmation_height_get (transaction, key.pub, confirmation_height));
	ASSERT_EQ (0, confirmation_height);
}
