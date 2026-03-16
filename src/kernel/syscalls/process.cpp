#include "../process_manager.hpp"

#include <sys/wait.h>

// Process management system calls.
// A few of them require sophisticated access to process state, so they are in `process_manager.cpp`.

extern "C"
{
pid_t getpid()
{
	return get_current_process().pid;
}

pid_t getppid()
{
	return get_current_process().ppid;
}

pid_t wait(int *stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}
}
