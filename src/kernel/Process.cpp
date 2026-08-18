#include "Process.hpp"
#include "process_manager.hpp"

#include <algorithm>

extern "C" typeof(close) libnds_close;

void Process::cleanup()
{
	for (const auto kernelfd : fdtable)
	{
		if (kernelfd <= STDERR_FILENO)
			// ignore standard streams and open slots
			continue;
		if (kernelfd_in_use(kernelfd))
			// kernelfd is in use by another process!
			continue;
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
