#include <windows.h>
namespace badem
{
void work_thread_reprioritize ()
{
	SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);
}
}
