#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/jsonconfig.hpp>
#include <badem/lib/timer.hpp>
#include <badem/node/node.hpp>
#include <badem/node/wallet.hpp>

#include <gtest/gtest.h>

TEST (work, one)
{
	badem::network_constants network_constants;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::change_block block (1, 1, badem::keypair ().prv, 3, 4);
	block.block_work_set (*pool.generate (block.root ()));
	uint64_t difficulty;
	ASSERT_FALSE (badem::work_validate (block, &difficulty));
	ASSERT_LT (network_constants.publish_threshold, difficulty);
}

TEST (work, disabled)
{
	badem::network_constants network_constants;
	badem::work_pool pool (0);
	auto result (pool.generate (badem::block_hash ()));
	ASSERT_FALSE (result.is_initialized ());
}

TEST (work, validate)
{
	badem::network_constants network_constants;
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::send_block send_block (1, 1, 2, badem::keypair ().prv, 4, 6);
	uint64_t difficulty;
	ASSERT_TRUE (badem::work_validate (send_block, &difficulty));
	ASSERT_LT (difficulty, network_constants.publish_threshold);
	send_block.block_work_set (*pool.generate (send_block.root ()));
	ASSERT_FALSE (badem::work_validate (send_block, &difficulty));
	ASSERT_LT (network_constants.publish_threshold, difficulty);
}

TEST (work, cancel)
{
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		badem::root key (1);
		pool.generate (key, [&done](boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_many)
{
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::root key1 (1);
	badem::root key2 (2);
	badem::root key3 (1);
	badem::root key4 (1);
	badem::root key5 (3);
	badem::root key6 (1);
	pool.generate (key1, [](boost::optional<uint64_t>) {});
	pool.generate (key2, [](boost::optional<uint64_t>) {});
	pool.generate (key3, [](boost::optional<uint64_t>) {});
	pool.generate (key4, [](boost::optional<uint64_t>) {});
	pool.generate (key5, [](boost::optional<uint64_t>) {});
	pool.generate (key6, [](boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

TEST (work, opencl)
{
	badem::logging logging;
	logging.init (badem::unique_path ());
	badem::logger_mt logger;
	bool error (false);
	badem::opencl_environment environment (error);
	ASSERT_FALSE (error);
	if (!environment.platforms.empty () && !environment.platforms.begin ()->devices.empty ())
	{
		badem::opencl_config config (0, 0, 16 * 1024);
		auto opencl (badem::opencl_work::create (true, config, logger));
		if (opencl != nullptr)
		{
			// 0 threads, should add 1 for managing OpenCL
			badem::work_pool pool (0, std::chrono::nanoseconds (0), [&opencl](badem::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
				return opencl->generate_work (root_a, difficulty_a);
			});
			ASSERT_NE (nullptr, pool.opencl);
			badem::root root;
			uint64_t difficulty (0xff00000000000000);
			uint64_t difficulty_add (0x000f000000000000);
			for (auto i (0); i < 16; ++i)
			{
				badem::random_pool::generate_block (root.bytes.data (), root.bytes.size ());
				auto result (*pool.generate (root, difficulty));
				uint64_t result_difficulty (0);
				ASSERT_FALSE (badem::work_validate (root, result, &result_difficulty));
				ASSERT_GE (result_difficulty, difficulty);
				difficulty += difficulty_add;
			}
		}
		else
		{
			std::cerr << "Error starting OpenCL test" << std::endl;
		}
	}
	else
	{
		std::cout << "Device with OpenCL support not found. Skipping OpenCL test" << std::endl;
	}
}

TEST (work, opencl_config)
{
	badem::opencl_config config1;
	config1.platform = 1;
	config1.device = 2;
	config1.threads = 3;
	badem::jsonconfig tree;
	config1.serialize_json (tree);
	badem::opencl_config config2;
	ASSERT_FALSE (config2.deserialize_json (tree));
	ASSERT_EQ (1, config2.platform);
	ASSERT_EQ (2, config2.device);
	ASSERT_EQ (3, config2.threads);
}

TEST (work, difficulty)
{
	badem::work_pool pool (std::numeric_limits<unsigned>::max ());
	badem::root root (1);
	uint64_t difficulty1 (0xff00000000000000);
	uint64_t difficulty2 (0xfff0000000000000);
	uint64_t difficulty3 (0xffff000000000000);
	uint64_t nonce1 (0);
	do
	{
		auto work1 = *pool.generate (root, difficulty1);
		badem::work_validate (root, work1, &nonce1);
	} while (nonce1 > difficulty2);
	ASSERT_GT (nonce1, difficulty1);
	uint64_t nonce2 (0);
	do
	{
		auto work2 = *pool.generate (root, difficulty2);
		badem::work_validate (root, work2, &nonce2);
	} while (nonce2 > difficulty3);
	ASSERT_GT (nonce2, difficulty2);
}

TEST (work, eco_pow)
{
	auto work_func = [](std::promise<std::chrono::nanoseconds> & promise, std::chrono::nanoseconds interval) {
		badem::work_pool pool (1, interval);
		constexpr auto num_iterations = 5;

		badem::timer<std::chrono::nanoseconds> timer;
		timer.start ();
		for (int i = 0; i < num_iterations; ++i)
		{
			badem::root root (1);
			uint64_t difficulty1 (0xff00000000000000);
			uint64_t difficulty2 (0xfff0000000000000);
			uint64_t nonce (0);
			do
			{
				auto work = *pool.generate (root, difficulty1);
				badem::work_validate (root, work, &nonce);
			} while (nonce > difficulty2);
			ASSERT_GT (nonce, difficulty1);
		}

		promise.set_value_at_thread_exit (timer.stop ());
	};

	std::promise<std::chrono::nanoseconds> promise1;
	std::future<std::chrono::nanoseconds> future1 = promise1.get_future ();
	std::promise<std::chrono::nanoseconds> promise2;
	std::future<std::chrono::nanoseconds> future2 = promise2.get_future ();

	std::thread thread1 (work_func, std::ref (promise1), std::chrono::nanoseconds (0));
	std::thread thread2 (work_func, std::ref (promise2), std::chrono::milliseconds (10));

	thread1.join ();
	thread2.join ();

	// Confirm that the eco pow rate limiter is working.
	// It's possible under some unlucky circumstances that this fails to the random nature of valid work generation.
	ASSERT_LT (future1.get (), future2.get ());
}
