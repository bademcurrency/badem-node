#include <badem/lib/memory.hpp>
#include <badem/node/common.hpp>

#include <gtest/gtest.h>
namespace badem
{
void cleanup_test_directories_on_exit ();
void force_badem_test_network ();
}

int main (int argc, char ** argv)
{
	badem::force_badem_test_network ();
	badem::set_use_memory_pools (false);
	badem::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	badem::cleanup_test_directories_on_exit ();
	return res;
}
