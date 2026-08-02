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

int setpgrp(void)
{
	return setpgid(0, 0);
}

pid_t wait(int *stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}

/**
 * tcgetpgrp - Get foreground process group ID
 * * For a no-op/single-user system, we mock this by returning the process group
 * of the calling process, making it look like it's always in the foreground.
 */
pid_t tcgetpgrp(int fd)
{
	// Optional: Validate that fd is open/valid if you track fds.
	// If (fd < 0 || fd >= MAX_FDS) { errno = EBADF; return -1; }

	// Hardcoding 1 or returning the calling process's pgid is standard for stubs.
	// If you have getpgid(0), use that. Otherwise, returning a safe default like 1 works.
	return 1;
}

/**
 * tcsetpgrp - Set foreground process group ID
 * * We mock this by pretending the foreground process group was successfully set.
 */
int tcsetpgrp(int fd, pid_t pgid)
{
	// Optional: Validate fd
	// If (fd < 0 || fd >= MAX_FDS) { errno = EBADF; return -1; }

	if (pgid < 0)
	{
		errno = EINVAL;
		return -1;
	}

	// Lie and say we successfully assigned the foreground process group
	return 0;
}

int setpgid(pid_t pid, pid_t pgid)
{
	auto &current = get_current_process();
	if (pid == 0)
		pid = current.pid;

	auto *process = get_process(pid);
	if (!process)
	{
		errno = ESRCH;
		return -1;
	}

	if (pgid < 0)
	{
		errno = EINVAL;
		return -1;
	}

	const auto current_pid = current.pid;
	if (process->pid != current_pid && process->ppid != current_pid)
	{
		errno = EPERM;
		return -1;
	}

	process->pgid = normalize_process_group_id(process->pid, pgid);
	return 0;
}
}
