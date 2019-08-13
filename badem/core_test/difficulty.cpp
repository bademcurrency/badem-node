#include <badem/lib/config.hpp>
#include <badem/lib/numbers.hpp>

#include <gtest/gtest.h>

TEST (difficulty, multipliers)
{
	{
		uint64_t base = 0xff00000000000000;
		uint64_t difficulty = 0xfff27e7a57c285cd;
		double expected_multiplier = 18.95461493377003;

		ASSERT_NEAR (expected_multiplier, badem::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, badem::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty = 0xfffffe0000000000;
		double expected_multiplier = 0.125;

		ASSERT_NEAR (expected_multiplier, badem::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, badem::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
#ifndef NDEBUG
		// Causes valgrind to be noisy
		if (!badem::running_within_valgrind ())
		{
			uint64_t base = 0xffffffc000000000;
			uint64_t difficulty_nil = 0;
			double multiplier_nil = 0.;

			ASSERT_DEATH_IF_SUPPORTED (badem::difficulty::to_multiplier (difficulty_nil, base), "");
			ASSERT_DEATH_IF_SUPPORTED (badem::difficulty::from_multiplier (multiplier_nil, base), "");
		}
#endif
	}
}

TEST (difficulty, network_constants)
{
	ASSERT_NEAR (16., badem::difficulty::to_multiplier (badem::network_constants::publish_full_threshold, badem::network_constants::publish_beta_threshold), 1e-10);
}
