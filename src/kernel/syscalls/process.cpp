#include <process_manager.hpp>

#include <sys/wait.h>

// Process management system calls.

// posix_spawn() and waitpid() require access to the process list,
// so they are in `process_manager.cpp`.

extern "C"
{
pid_t getpid()
{
	puts("getpid: called");
	const auto pid = get_current_process().pid;
	printf("getpid: returning %d\n", pid);
	return pid;
}

pid_t getppid()
{
	puts("getppid: called");
	const auto ppid = get_current_process().ppid;
	printf("getppid: returning %d\n", ppid);
	return ppid;
}

pid_t getpgrp()
{
	return get_current_process().pgid;
}

pid_t wait(int *stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}
}
