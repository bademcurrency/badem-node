#include <badem/core_test/testutil.hpp>
#include <badem/node/node.hpp>
#include <badem/node/testing.hpp>
#include <badem/secure/versioning.hpp>

#include <gtest/gtest.h>

#include <boost/polymorphic_cast.hpp>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	badem::system system (24000, 1);
	bool error (false);
	badem::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	badem::keypair random_key;
	badem::uint256_union id (random_key.pub);
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	badem::system system (24000, 1);
	badem::keypair random_key;
	badem::uint256_union id (random_key.pub);
	{
		bool error (false);
		badem::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		badem::raw_key password;
		password.data.clear ();
		system.deadline_set (10s);
		while (password.data == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		badem::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	badem::system system (24000, 1);
	badem::uint256_union one (1);
	{
		bool error (false);
		badem::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		bool error (false);
		badem::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

TEST (wallets, upgrade)
{
	badem::system system (24000, 1);
	auto path (badem::unique_path ());
	badem::keypair id;
	{
		badem::node_init init1;
		auto node1 (std::make_shared<badem::node> (init1, system.io_ctx, 24001, path, system.alarm, system.logging, system.work));
		ASSERT_FALSE (init1.error ());
		bool error (false);
		badem::wallets wallets (error, *node1);
		wallets.create (id.pub);
		auto transaction_source (node1->wallets.env.tx_begin_write ());
		auto tx_source = static_cast<MDB_txn *> (transaction_source.get_handle ());
		auto & mdb_store (dynamic_cast<badem::mdb_store &> (node1->store));
		auto transaction_destination (mdb_store.tx_begin_write ());
		auto tx_destination = static_cast<MDB_txn *> (transaction_destination.get_handle ());
		wallets.move_table (id.pub.to_string (), tx_source, tx_destination);
		node1->store.version_put (transaction_destination, 11);

		badem::account_info info;
		ASSERT_FALSE (mdb_store.account_get (transaction_destination, badem::genesis_account, info));
		badem::account_info_v13 account_info_v13 (info.head, info.rep_block, info.open_block, info.balance, info.modified, info.block_count, info.epoch);
		auto status (mdb_put (mdb_store.env.tx (transaction_destination), mdb_store.get_account_db (info.epoch) == badem::block_store_partial<MDB_val, badem::mdb_store>::tables::accounts_v0 ? mdb_store.accounts_v0 : mdb_store.accounts_v1, badem::mdb_val (badem::test_genesis_key.pub), badem::mdb_val (account_info_v13), 0));
		(void)status;
		assert (status == 0);
	}
	badem::node_init init1;
	auto node1 (std::make_shared<badem::node> (init1, system.io_ctx, 24001, path, system.alarm, system.logging, system.work));
	ASSERT_EQ (1, node1->wallets.items.size ());
	ASSERT_EQ (id.pub, node1->wallets.items.begin ()->first);
	auto transaction_new (node1->wallets.env.tx_begin_write ());
	auto tx_new = static_cast<MDB_txn *> (transaction_new.get_handle ());
	auto transaction_old (node1->store.tx_begin_write ());
	auto tx_old = static_cast<MDB_txn *> (transaction_old.get_handle ());
	MDB_dbi old_handle;
	ASSERT_EQ (MDB_NOTFOUND, mdb_dbi_open (tx_old, id.pub.to_string ().c_str (), 0, &old_handle));
	MDB_dbi new_handle;
	ASSERT_EQ (0, mdb_dbi_open (tx_new, id.pub.to_string ().c_str (), 0, &new_handle));
}

// Keeps breaking whenever we add new DBs
TEST (wallets, DISABLED_wallet_create_max)
{
	badem::system system (24000, 1);
	bool error (false);
	badem::wallets wallets (error, *system.nodes[0]);
	const int nonWalletDbs = 19;
	for (int i = 0; i < system.nodes[0]->config.lmdb_max_dbs - nonWalletDbs; i++)
	{
		badem::keypair key;
		auto wallet = wallets.create (key.pub);
		auto existing = wallets.items.find (key.pub);
		ASSERT_TRUE (existing != wallets.items.end ());
		badem::raw_key seed;
		seed.data = 0;
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		existing->second->store.seed_set (transaction, seed);
	}
	badem::keypair key;
	wallets.create (key.pub);
	auto existing = wallets.items.find (key.pub);
	ASSERT_TRUE (existing == wallets.items.end ());
}

TEST (wallets, reload)
{
	badem::system system (24000, 1);
	badem::uint256_union one (1);
	bool error (false);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, system.nodes[0]->wallets.items.size ());
	{
		std::lock_guard<std::mutex> lock_wallet (system.nodes[0]->wallets.mutex);
		badem::inactive_node node (system.nodes[0]->application_path, 24001);
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	system.deadline_set (5s);
	while (system.nodes[0]->wallets.open (one) == nullptr)
	{
		system.poll ();
	}
	ASSERT_EQ (2, system.nodes[0]->wallets.items.size ());
}

TEST (wallets, vote_minimum)
{
	badem::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	badem::keypair key1;
	badem::keypair key2;
	badem::genesis genesis;
	badem::state_block send1 (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, std::numeric_limits<badem::uint128_t>::max () - node1.config.vote_minimum.number (), key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (genesis.hash ()));
	ASSERT_EQ (badem::process_result::progress, node1.process (send1).code);
	badem::state_block open1 (key1.pub, 0, key1.pub, node1.config.vote_minimum.number (), send1.hash (), key1.prv, key1.pub, system.work.generate (key1.pub));
	ASSERT_EQ (badem::process_result::progress, node1.process (open1).code);
	// send2 with amount vote_minimum - 1 (not voting representative)
	badem::state_block send2 (badem::test_genesis_key.pub, send1.hash (), badem::test_genesis_key.pub, std::numeric_limits<badem::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1, key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send1.hash ()));
	ASSERT_EQ (badem::process_result::progress, node1.process (send2).code);
	badem::state_block open2 (key2.pub, 0, key2.pub, node1.config.vote_minimum.number () - 1, send2.hash (), key2.prv, key2.pub, system.work.generate (key2.pub));
	ASSERT_EQ (badem::process_result::progress, node1.process (open2).code);
	auto wallet (node1.wallets.items.begin ()->second);
	ASSERT_EQ (0, wallet->representatives.size ());
	wallet->insert_adhoc (badem::test_genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.compute_reps ();
	ASSERT_EQ (2, wallet->representatives.size ());
}
