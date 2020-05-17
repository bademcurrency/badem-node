#include "gtest/gtest.h"

#include <badem/node/common.hpp>
#include <badem/node/logging.hpp>

namespace badem
{
void cleanup_test_directories_on_exit ();
void force_badem_test_network ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	badem::force_badem_test_network ();
	badem::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	// Setting up logging so that there aren't any piped to standard output.
	badem::logging logging;
	logging.init (badem::unique_path ());
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	badem::cleanup_test_directories_on_exit ();
	return res;
}
