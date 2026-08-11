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

	static constexpr auto MAX_FDS = 8;

	std::shared_ptr<void> dlhandle;
	int pid{};
	int ppid{-1};
	int pgid{};
	int fdtable[MAX_FDS]{-1, -1, -1, -1, -1, -1, -1, -1};
	int fdflags[MAX_FDS]{};
	int fdstatus[MAX_FDS]{};
	std::vector<cothread_t> threads;
	CStrArray argv, envp;
	MainFn entrypoint{};
	int exit_code{};
	int status{};
	std::array<struct sigaction, NSIG> signal_actions;
	sigset_t signal_mask;
	sigset_t pending_signals;
	jmp_buf vfork_env, exit_env;
	bool is_vfork_suspended{};
	uint64_t alarm_target_ticks{}; // 0 means no active alarm
	bool alarm_active{};

	~Process();
	bool all_threads_joined();
	void check_alarm();

	// Returns `Process::MAX_FDS` if all slots are taken.
	int find_first_open_fd_slot();
};

constexpr bool operator==(const Process &a, const Process &b)
{
	return a.pid == b.pid;
}
