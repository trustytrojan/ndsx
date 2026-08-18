#pragma once

#include <nds.h>

#include "Process.hpp"

Process &get_current_process();
Process *get_process(pid_t pid);
Process *get_process_by_thread(cothread_t thread);
bool deliver_pending_signals(Process &process, bool *caught_signal = nullptr);
void set_kernel_process();

struct nds_critical_section
{
	const int oldIME = enterCriticalSection();
	constexpr ~nds_critical_section() { leaveCriticalSection(oldIME); }
};

void transfer_current_thread(Process &from, Process &to);
bool kernelfd_in_use(int kfd);

static constexpr bool is_valid_signal_number(const int sig)
{
	return sig > 0 && sig < NSIG;
}

static void queue_signal(Process &process, const int sig)
{
	sigaddset(&process.pending_signals, sig);
}

static constexpr pid_t normalize_process_group_id(pid_t pid, pid_t pgid)
{
	return (pgid == 0) ? pid : pgid;
}
