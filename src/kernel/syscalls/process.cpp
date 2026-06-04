#include <process_manager.hpp>

#include <sys/wait.h>

// Process management system calls.

// posix_spawn() and waitpid() require access to the process list,
// so they are in `process_manager.cpp`.

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

pid_t getpgrp()
{
	return get_current_process().pgid;
}

int killpg(int pgrp, int sig)
{
	return kill_process_group(pgrp, sig);
}

int setpgid(pid_t pid, pid_t pgid)
{
	return set_process_group(pid, pgid);
}

pid_t wait(int *stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}
}
