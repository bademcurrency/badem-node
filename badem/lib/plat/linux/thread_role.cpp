#include <badem/lib/utility.hpp>

#include <pthread.h>

void badem::thread_role::set_os_name (std::string const & thread_name)
{
	pthread_setname_np (pthread_self (), thread_name.c_str ());
}
