#pragma once

#include <nds.h>
#include <pthread.h>
#include <array>
#include <vector>
#include <signal.h>

#include "CStrArray.hpp"

struct Process
{
	using MainFn = int (*)(int argc, char *argv[], char *envp[]);

	static constexpr auto MAX_FDS{8};

	void *dlhandle;
	int pid;
	int ppid;
	int pgid;
	int fdtable[MAX_FDS];
	std::vector<cothread_t> threads;
	CStrArray argv, envp;
	MainFn entrypoint;
	int exit_code;
	int status;
	std::array<struct sigaction, NSIG> signal_actions;
	sigset_t signal_mask;
	sigset_t pending_signals;
	

	constexpr Process()
		: dlhandle(nullptr),
		  pid(0),
		  ppid(-1),
		  pgid(0),
		  fdtable{-1, -1, -1, -1, -1, -1, -1, -1},
		  entrypoint(nullptr),
		  exit_code(0),
		  status(0)
	{
	}

	constexpr ~Process();
	constexpr bool all_threads_joined();
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
