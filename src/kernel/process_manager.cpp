#include "process_manager.hpp"

#include <dlfcn.h>
#include <spawn.h>

#include <cerrno>
#include <list>

constexpr Process::~Process()
{
	if (dlhandle && dlclose(dlhandle) < 0)
		printf("kernel: dlclose: %s\n", dlerror());
	for (const auto fd : fdtable)
	{
		// We don't want to close libnds's standard streams! Close everything else, though.
		if (fd > STDERR_FILENO)
			close(fd);
	}
}

constexpr bool Process::all_threads_joined()
{
	for (const auto thread : threads)
		if (thread > 0 && !cothread_has_joined(thread))
			return false;
	return true;
}

std::list<Process> processes{};

static Process &kernel_process = processes.emplace_back();
static int global_pid_counter = 0;

void set_kernel_process()
{
	kernel_process.pid = 0;
	kernel_process.ppid = -1;
	kernel_process.threads[0] = cothread_get_current();
	kernel_process.fdtable[0] = 0;
	kernel_process.fdtable[1] = 1;
	kernel_process.fdtable[2] = 2;
}

Process &get_current_process()
{
	const auto self = cothread_get_current();
	for (auto &p : processes)
	{
		for (const auto t : p.threads)
			if (t == self)
				return p;
	}
	puts("kernel: get_current_process: current process not found! crashing");
	libndsCrash("current process not found");
}

auto get_process_itr(pid_t pid)
{
	return std::ranges::find_if(processes, [=](auto &p) { return p.pid == pid; });
}

Process *get_process(pid_t pid)
{
	const auto itr = get_process_itr(pid);
	return (itr == processes.end()) ? nullptr : &*itr;
}

static int process_start_trampoline(Process *arg)
{
	const auto p = (Process *)arg;
	p->exit_code = p->entrypoint(p->argc, p->argv, p->envp);

	// No mechanism for anything other than normal process exits exist, so that's all we will store.
	p->status = (p->exit_code << 8) | 0x00;
	p->has_wait_status = true;

	return p->exit_code;
}

extern "C" int posix_spawn(
	pid_t *pid,
	const char *path,
	const posix_spawn_file_actions_t *file_actions,
	const posix_spawnattr_t *attrp,
	char *const argv[],
	char *const envp[])
{
	const auto &parent = get_current_process();
	(void)file_actions;
	(void)attrp;

	const auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle)
	{
		printf("kernel: dlopen: %s\n", dlerror());
		return -1;
	}

	const auto entrypoint = (MainFn)dlsym(handle, "main");
	if (!entrypoint)
	{
		printf("dlsym: %s\n", dlerror());
		return -1;
	}

	// Count arguments
	int argc = 0;
	if (argv)
		for (; argv[argc]; ++argc)
			;

	// Create the process object first, then create the thread with a stable address.
	auto &child = processes.emplace_back();
	child.dlhandle = handle;
	child.pid = ++global_pid_counter;
	child.ppid = parent.pid;
	child.argc = argc;
	child.argv = const_cast<char **>(argv);
	child.envp = const_cast<char **>(envp);
	child.entrypoint = entrypoint;
	child.has_wait_status = false;

	child.fdtable[0] = 0;
	child.fdtable[1] = 1;
	child.fdtable[2] = 2;

	// Attempt to create the process's first thread
	child.threads[0] = cothread_create((cothread_entrypoint_t)process_start_trampoline, &child, 0, 0);
	if (child.threads[0] == -1)
	{
		perror("cothread_create");
		processes.erase(get_process_itr(child.pid));
		return -1;
	}

	if (pid)
		*pid = child.pid;

	return 0;
}

static bool waitpid_pid_matches(const Process &p, pid_t parent_pid, pid_t pid)
{
	if (p.ppid != parent_pid)
		return false;

	if (pid == -1)
		return true;

	if (pid > 0)
		return p.pid == pid;

	// Process groups are not modeled yet; approximate pid==0 as "any child in caller group".
	if (pid == 0)
		return true;

	return false;
}

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
	const auto parent_pid = getpid();

	int supported_options = WNOHANG;
#ifdef WUNTRACED
	supported_options |= WUNTRACED;
#endif
#ifdef WCONTINUED
	supported_options |= WCONTINUED;
#endif

	if (options & ~supported_options)
	{
		errno = EINVAL;
		return -1;
	}

	if (pid < -1)
	{
		// We do not track process groups yet.
		puts("waitpid: pid < -1");
		errno = ECHILD;
		return -1;
	}

	const bool nohang = (options & WNOHANG) != 0;

	for (;;)
	{
		bool has_matching_child = false;

		for (auto it = processes.begin(); it != processes.end(); ++it)
		{
			auto &p = *it;
			if (!waitpid_pid_matches(p, parent_pid, pid))
				continue;

			has_matching_child = true;

			if (!p.all_threads_joined())
				continue;

			const int status = p.has_wait_status ? p.status : ((cothread_get_exit_code(p.threads[0]) & 0xff) << 8);
			const pid_t child_pid = p.pid;

			if (stat_loc)
				*stat_loc = status;

			// Consume the child's wait status (reap).
			it = processes.erase(it);
			return child_pid;
		}

		if (!has_matching_child)
		{
			puts("waitpid: !has_matching_child");
			errno = ECHILD;
			return -1;
		}

		if (nohang)
			return 0;

		cothread_yield();
	}
}

extern "C" pid_t wait(int *stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}
