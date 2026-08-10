#include "Process.hpp"
#include "pipe_ops.hpp"
#include "process_manager.hpp"

#include <algorithm>

extern "C" typeof(close) libnds_close;

void Process::cleanup()
{
	printf("Process::cleanup: pid: %d\n", pid);
	for (const auto kernelfd : fdtable)
	{
		if (kernelfd <= STDERR_FILENO)
			// ignore standard streams and open slots
			continue;
		if (kernelfd_in_use(kernelfd))
			// kernelfd is in use by another process!
			continue;
		if (_pipe::is_kfd(kernelfd))
		{
			// TODO: maybe make _exit() cleanup the vfork()'d child that calls it?
			printf("Process::cleanup: pipe kfd: %x\n", kernelfd);
			_pipe::close(kernelfd);
			continue;
		}
		printf("Process::cleanup: closing %x\n", kernelfd);
		if (libnds_close(kernelfd) == -1)
			perror("libnds_close");
	}
	for (const auto t : threads)
	{
		// The only possible errors are:
		// - EPERM:  Deleting the current thread
		// - EINVAL: Thread not in list
		// Neither are fatal problems, so we can ignore.
		cothread_delete(t);
	}
}

bool Process::all_threads_joined()
{
	return std::ranges::all_of(
		threads,
		[](const auto t)
		{
			errno = 0;
			return t <= 0 || // Open slot
				   cothread_has_joined(t) ||
				   errno == EINVAL; // Detached threads are deleted by libnds as soon as they finish.
		});
}

void Process::check_alarm()
{
	if (!alarm_active)
		return;

	uint64_t now = systemCounterGetTicks();
	if (now >= alarm_target_ticks)
	{
		alarm_active = false;
		alarm_target_ticks = 0;

		// Queue SIGALRM to the process
		sigaddset(&pending_signals, SIGALRM);
	}
}

int Process::find_first_open_fd_slot()
{
	int i = 0;
	for (; i < Process::MAX_FDS && fdtable[i] >= 0; ++i)
		;
	return i;
}
