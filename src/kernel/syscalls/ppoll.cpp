#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include <nds/cothread.h>

#include "process_manager.hpp"

extern "C"
{
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
{
	// POSIX check: negative timespec or invalid nsec values return EINVAL
	if (tmo_p != nullptr)
	{
		if (tmo_p->tv_sec < 0 || tmo_p->tv_nsec < 0 || tmo_p->tv_nsec >= 1000000000L)
		{
			errno = EINVAL;
			return -1;
		}
	}

	auto &process = get_current_process();
	const sigset_t orig_mask = process.signal_mask;

	// 1. Temporarily apply sigmask if provided (atomically swap signal mask)
	if (sigmask != nullptr)
	{
		process.signal_mask = *sigmask;
		// Ensure SIGKILL/SIGSTOP remain unblocked per POSIX standard
		sigdelset(&process.signal_mask, SIGKILL);
		sigdelset(&process.signal_mask, SIGSTOP);
	}

	// 2. Check for pending signals before polling
	bool signal_caught = false;
	if (deliver_pending_signals(process, &signal_caught))
	{
		process.signal_mask = orig_mask;
		if (signal_caught)
		{
			errno = EINTR;
			return -1;
		}
	}

	// 3. Compute target end time for timeout (if tmo_p != NULL)
	bool use_timeout = (tmo_p != nullptr);
	struct timeval start_time{}, current_time{};
	long long timeout_ns = 0;

	if (use_timeout)
	{
		timeout_ns = ((long long)tmo_p->tv_sec * 1000000000LL) + tmo_p->tv_nsec;
		gettimeofday(&start_time, nullptr);
	}

	int ret = 0;

	// 4. Main Event Loop
	for (;;)
	{
		// Run non-blocking poll via dswifi / lwip (timeout = 0)
		ret = poll(fds, nfds, 0);

		// If I/O event detected or an error occurred (-1), break immediately
		if (ret != 0)
			break;

		// Check for incoming signals during poll wait
		if (deliver_pending_signals(process, &signal_caught))
		{
			process.signal_mask = orig_mask;
			if (signal_caught)
			{
				errno = EINTR;
				return -1;
			}
		}

		// Check timeout condition
		if (use_timeout)
		{
			if (timeout_ns <= 0)
			{
				ret = 0; // Immediate timeout or zero timeout requested
				break;
			}

			gettimeofday(&current_time, nullptr);

			long long elapsed_sec = (long long)current_time.tv_sec - start_time.tv_sec;
			long long elapsed_usec = (long long)current_time.tv_usec - start_time.tv_usec;
			long long elapsed_ns = (elapsed_sec * 1000000000LL) + (elapsed_usec * 1000LL);

			if (elapsed_ns >= timeout_ns)
			{
				ret = 0; // Timed out
				break;
			}
		}

		// Yield CPU to allow dswifi background threads and other processes to run
		cothread_yield();
	}

	// 5. Restore original signal mask upon exit
	process.signal_mask = orig_mask;

	return ret;
}
}
