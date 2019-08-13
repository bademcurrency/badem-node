#include <badem/core_test/testutil.hpp>
#include <badem/lib/jsonconfig.hpp>
#include <badem/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (active_transactions, bounded_active_elections)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 5;
	auto & node1 = *system.add_node (node_config);
	badem::genesis genesis;
	size_t count (1);
	auto send (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - count * badem::RAW_ratio, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto previous_size = node1.active.size ();
	bool done (false);
	system.deadline_set (5s);
	while (!done)
	{
		count++;
		node1.process_active (send);
		done = previous_size > node1.active.size ();
		ASSERT_LT (node1.active.size (), node1.config.active_elections_size); //triggers after reverting #2116
		ASSERT_NO_ERROR (system.poll ());
		auto previous_hash = send->hash ();
		send = std::make_shared<badem::state_block> (badem::test_genesis_key.pub, previous_hash, badem::test_genesis_key.pub, badem::genesis_amount - count * badem::RAW_ratio, badem::test_genesis_key.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (previous_hash));
		previous_size = node1.active.size ();
		//sleep this thread for the max delay between request loop rounds possible for such a small active_elections_size
		std::this_thread::sleep_for (std::chrono::milliseconds (node1.network_params.network.request_interval_ms + (node_config.active_elections_size * 20)));
	}
}

TEST (active_transactions, adjusted_difficulty_priority)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	badem::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	badem::genesis genesis;
	badem::keypair key1, key2, key3;

	auto send1 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - 10 * badem::RAW_ratio, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, send1->hash (), badem::test_genesis_key.pub, badem::genesis_amount - 20 * badem::RAW_ratio, key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<badem::state_block> (key1.pub, 0, key1.pub, 10 * badem::RAW_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<badem::state_block> (key2.pub, 0, key2.pub, 10 * badem::RAW_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (open1);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it = node1.active.roots.begin ();
		}
	}

	system.deadline_set (10s);
	while (node1.active.confirmed.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, send2->hash (), badem::test_genesis_key.pub, 9 * badem::RAW_ratio, key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send2->hash (), badem::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, send3->hash (), badem::test_genesis_key.pub, 8 * badem::RAW_ratio, key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send3->hash (), badem::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<badem::state_block> (key1.pub, open1->hash (), key1.pub, 9 * badem::RAW_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (open1->hash (), badem::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<badem::state_block> (key1.pub, send5->hash (), key1.pub, 8 * badem::RAW_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (send5->hash (), badem::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<badem::state_block> (key2.pub, open2->hash (), key2.pub, 9 * badem::RAW_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash (), badem::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<badem::state_block> (key2.pub, send7->hash (), key2.pub, 8 * badem::RAW_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (send7->hash (), badem::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));

	node1.process_active (send3); //genesis
	node1.process_active (send5); //key1
	node1.process_active (send7); //key2
	node1.process_active (send4); //genesis
	node1.process_active (send6); //key1
	node1.process_active (send8); //key2

	system.deadline_set (10s);
	while (node1.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::lock_guard<std::mutex> lock (node1.active.mutex);
	uint64_t last_adjusted (0);
	for (auto i (node1.active.roots.get<1> ().begin ()), n (node1.active.roots.get<1> ().end ()); i != n; ++i)
	{
		//first root has nothing to compare
		if (last_adjusted != 0)
		{
			ASSERT_LT (i->adjusted_difficulty, last_adjusted);
		}
		last_adjusted = i->adjusted_difficulty;
	}
}

TEST (active_transactions, keep_local)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 3; //bound to 3, wont drop wallet created transactions, but good to test dropping remote
	//delay_frontier_confirmation_height_updating to allow the test to before
	badem::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	badem::genesis genesis;
	//key 1/2 will be managed by the wallet
	badem::keypair key1, key2, key3, key4;
	wallet.insert_adhoc (badem::test_genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	auto send1 (wallet.send_action (badem::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	auto send2 (wallet.send_action (badem::test_genesis_key.pub, key2.pub, node1.config.receive_minimum.number ()));
	auto send3 (wallet.send_action (badem::test_genesis_key.pub, key3.pub, node1.config.receive_minimum.number ()));
	auto send4 (wallet.send_action (badem::test_genesis_key.pub, key4.pub, node1.config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			(it->election)->confirm_once ();
			it = node1.active.roots.begin ();
		}
	}
	auto open1 (std::make_shared<badem::state_block> (key3.pub, 0, key3.pub, badem::RAW_ratio, send3->hash (), key3.prv, key3.pub, system.work.generate (key3.pub)));
	node1.process_active (open1);
	auto open2 (std::make_shared<badem::state_block> (key4.pub, 0, key4.pub, badem::RAW_ratio, send4->hash (), key4.prv, key4.pub, system.work.generate (key4.pub)));
	node1.process_active (open2);
	//none are dropped since none are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto send5 (wallet.send_action (badem::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	node1.active.start (send5);
	//drop two lowest non-wallet managed active_transactions before inserting a new into active as all are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (active_transactions, prioritize_chains)
{
	badem::system system;
	badem::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 4; //bound to 3, wont drop wallet created transactions, but good to test dropping remote
	//delay_frontier_confirmation_height_updating to allow the test to before
	badem::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	badem::genesis genesis;
	badem::keypair key1, key2, key3;

	auto send1 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, genesis.hash (), badem::test_genesis_key.pub, badem::genesis_amount - 10 * badem::RAW_ratio, key1.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto open1 (std::make_shared<badem::state_block> (key1.pub, 0, key1.pub, 10 * badem::RAW_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto send2 (std::make_shared<badem::state_block> (key1.pub, open1->hash (), key1.pub, badem::RAW_ratio * 9, key2.pub, key1.prv, key1.pub, system.work.generate (open1->hash ())));
	auto send3 (std::make_shared<badem::state_block> (key1.pub, send2->hash (), key1.pub, badem::RAW_ratio * 8, key2.pub, key1.prv, key1.pub, system.work.generate (send2->hash ())));
	auto send4 (std::make_shared<badem::state_block> (key1.pub, send3->hash (), key1.pub, badem::RAW_ratio * 7, key2.pub, key1.prv, key1.pub, system.work.generate (send3->hash ())));
	auto send5 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, send1->hash (), badem::test_genesis_key.pub, badem::genesis_amount - 20 * badem::RAW_ratio, key2.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto send6 (std::make_shared<badem::state_block> (badem::test_genesis_key.pub, send5->hash (), badem::test_genesis_key.pub, badem::genesis_amount - 30 * badem::RAW_ratio, key3.pub, badem::test_genesis_key.prv, badem::test_genesis_key.pub, system.work.generate (send5->hash ())));
	auto open2 (std::make_shared<badem::state_block> (key2.pub, 0, key2.pub, 10 * badem::RAW_ratio, send5->hash (), key2.prv, key2.pub, system.work.generate (key2.pub, badem::difficulty::from_multiplier (50., node1.network_params.network.publish_threshold))));
	uint64_t difficulty1 (0);
	badem::work_validate (*open2, &difficulty1);
	uint64_t difficulty2 (0);
	badem::work_validate (*send6, &difficulty2);

	node1.process_active (send1);
	node1.process_active (open1);
	node1.process_active (send5);
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it = node1.active.roots.get<1> ().begin ();
		}
	}

	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (send6);

	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	bool done (false);
	//wait for all to be long_unconfirmed
	while (!done)
	{
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	std::this_thread::sleep_for (1s);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	//wait for all to be long_unconfirmed
	done = false;
	system.deadline_set (10s);
	while (!done)
	{
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	size_t seen (0);
	{
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			if (it->difficulty == (difficulty1 || difficulty2))
			{
				seen++;
			}
			it++;
		}
	}
	ASSERT_LT (seen, 2);
	ASSERT_EQ (node1.active.size (), 4);
}
