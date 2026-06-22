#pragma once

#include <array>
#include <csetjmp>
#include <memory>
#include <nds.h>
#include <signal.h>
#include <vector>

#include "CStrArray.hpp"

struct Process
{
	using MainFn = int (*)(int argc, char *argv[], char *envp[]);

	static constexpr auto MAX_FDS{8};

	std::shared_ptr<void> dlhandle;
	int pid;
	int ppid;
	int pgid;
	int fdtable[MAX_FDS];
	int fdflags[MAX_FDS];
	int fdstatus[MAX_FDS];
	std::vector<cothread_t> threads;
	CStrArray argv, envp;
	MainFn entrypoint;
	int exit_code;
	int status;
	std::array<struct sigaction, NSIG> signal_actions;
	sigset_t signal_mask;
	sigset_t pending_signals;
	jmp_buf vfork_env, exit_env;
	bool is_vfork_suspended = false;

	constexpr Process()
		: dlhandle(nullptr),
		  pid(0),
		  ppid(-1),
		  pgid(0),
		  fdtable{-1, -1, -1, -1, -1, -1, -1, -1},
		  fdflags{0, 0, 0, 0, 0, 0, 0, 0},
		  fdstatus{0, 0, 0, 0, 0, 0, 0, 0},
		  entrypoint(nullptr),
		  exit_code(0),
		  status(0)
	{
	}

	constexpr ~Process() { cleanup(); }
	bool all_threads_joined();
	void cleanup();
};

constexpr bool operator==(const Process &a, const Process &b)
{
	return a.pid == b.pid;
}

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
