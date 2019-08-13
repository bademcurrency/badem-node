#include <badem/lib/config.hpp>

#include <valgrind/valgrind.h>

namespace badem
{
void force_badem_test_network ()
{
	badem::network_constants::set_active_network (badem::badem_networks::badem_test_network);
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}
}
