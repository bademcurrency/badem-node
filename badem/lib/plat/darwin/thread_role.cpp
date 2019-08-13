#include <badem/lib/utility.hpp>

#include <pthread.h>

void badem::thread_role::set_os_name (std::string const & thread_name)
{
	pthread_setname_np (thread_name.c_str ());
}
