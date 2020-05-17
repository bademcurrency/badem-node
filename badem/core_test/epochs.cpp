#include <badem/secure/common.hpp>
#include <badem/secure/epoch.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	badem::epochs epochs;
	// Test epoch 1
	badem::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (badem::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (badem::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), badem::epoch::epoch_1);

	// Test epoch 2
	badem::keypair key2;
	epochs.add (badem::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (badem::epoch::epoch_2));
	ASSERT_EQ (badem::uint256_union (link1), epochs.link (badem::epoch::epoch_1));
	ASSERT_EQ (badem::uint256_union (link2), epochs.link (badem::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), badem::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (badem::epochs::is_sequential (badem::epoch::epoch_0, badem::epoch::epoch_1));
	ASSERT_TRUE (badem::epochs::is_sequential (badem::epoch::epoch_1, badem::epoch::epoch_2));

	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::epoch_0, badem::epoch::epoch_2));
	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::epoch_0, badem::epoch::invalid));
	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::unspecified, badem::epoch::epoch_1));
	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::epoch_1, badem::epoch::epoch_0));
	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::epoch_2, badem::epoch::epoch_0));
	ASSERT_FALSE (badem::epochs::is_sequential (badem::epoch::epoch_2, badem::epoch::epoch_2));
}
