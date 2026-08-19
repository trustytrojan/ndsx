#include "Process.hpp"
#include "pipe_ops.hpp"
#include "process_manager.hpp"

#include <algorithm>

extern "C" typeof(close) libnds_close;

Process::~Process()
{
	printf("~Process: pid=%d\n", pid);

	for (const auto kfd : fdtable)
	{
		if (kfd <= STDERR_FILENO)
			// ignore standard streams and open slots
			continue;

		if (kernelfd_in_use(kfd))
			// kernelfd is in use by another process!
			continue;

		if (_pipe::is_kfd(kfd))
		{
			printf("unclosed pipe kfd: %x\n", kfd);

			if (_pipe::close(kfd) == -1)
				perror("_pipe::close");

			continue;
		}

		printf("unclosed libnds kfd: %x\n", kfd);

		if (libnds_close(kfd) == -1)
			perror("libnds_close");
	}

	// The only possible errors of cothread_delete() are:
	// - EPERM:  Deleting the current thread
	// - EINVAL: Thread not in list
	// Neither are fatal problems, so we can ignore them.
	std::ranges::for_each(threads, cothread_delete);
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
