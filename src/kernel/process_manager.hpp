#pragma once

#include <nds.h>

using MainFn = int (*)(int argc, char *argv[], char *envp[]);

struct Process
{
	static constexpr auto MAX_FDS{8}, MAX_THREADS{8};

	void *dlhandle;
	int pid;
	int ppid;
	int fdtable[MAX_FDS];
	cothread_t threads[MAX_THREADS];
	int argc;
	char **argv;
	char **envp;
	MainFn entrypoint;
	int exit_code;
	int status;
	bool has_wait_status;

	constexpr Process() : dlhandle(nullptr), pid(0), ppid(-1), fdtable{-1, -1, -1, -1, -1, -1, -1, -1}, threads{0, 0, 0, 0, 0, 0, 0, 0}, argc(0), argv(nullptr), envp(nullptr), entrypoint(nullptr), exit_code(0), status(0), has_wait_status(false) {}
	constexpr ~Process();
	constexpr bool all_threads_joined();
};

constexpr bool operator==(const Process &a, const Process &b)
{
	return a.pid == b.pid;
}

Process &get_current_process();
Process *get_process(pid_t pid);
void set_kernel_process();
