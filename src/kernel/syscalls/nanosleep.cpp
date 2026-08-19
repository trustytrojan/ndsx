// SPDX-License-Identifier: Zlib
// Copyright (C) 2026 Antonio Niño Díaz

#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <nds/cothread.h>
#include <nds/system_counter.h>

#include "process_manager.hpp" // For deliver_pending_signals() and get_current_process()

extern "C" int nanosleep(const struct timespec *req, struct timespec *rem)
{
    // 1. POSIX Validation
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    uint64_t now = systemCounterGetTicks();
    
    // Calculate total requested ticks from seconds and nanoseconds
    uint64_t sec_ticks = systemCounterUsecsToTicks((uint64_t)req->tv_sec * 1000000ULL);
    uint64_t nsec_ticks = systemCounterUsecsToTicks((uint64_t)req->tv_nsec / 1000ULL);
    uint64_t total_ticks = sec_ticks + nsec_ticks;
    uint64_t end = now + total_ticks;

    auto &process = get_current_process();

    while (1)
    {
        now = systemCounterGetTicks();
        if (now >= end)
            return 0;

        // 2. Check for signal delivery during wait
        bool signal_caught = false;
        if (deliver_pending_signals(process, &signal_caught))
        {
            if (signal_caught)
            {
                // Calculate remaining time if requested by caller
                if (rem)
                {
                    uint64_t remaining_ticks = end - now;
                    // Convert ticks back to microseconds for timespec calculation
                    uint64_t remaining_usecs = systemCounterTicksToUsec(remaining_ticks);

                    rem->tv_sec = remaining_usecs / 1000000ULL;
                    rem->tv_nsec = (remaining_usecs % 1000000ULL) * 1000ULL;
                }

                errno = EINTR;
                return -1;
            }
        }

        // Only yield if we have to wait for a significant number of ticks
        if ((end - now) > 100)
            cothread_yield();
    }
}
