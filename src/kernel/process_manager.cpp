#include <nds/cothread.h>
#include <process_manager.hpp>

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>

#include <errno.h>
#include <list>

constexpr Process::~Process()
{
	if (dlhandle && dlclose(dlhandle) < 0)
		printf("kernel: dlclose: %s\n", dlerror());
	for (const auto fd : fdtable)
	{
		// We don't want to close libnds's standard streams! Close everything else, though.
		if (fd > STDERR_FILENO && close(fd) == -1)
			perror("kernel: close");
	}
	for (const auto t : threads)
	{
		// printf("kernel: deleting thread %d", t);

		// The only possible errors are:
		// - EPERM:  Deleting the current thread
		// - EINVAL: Thread not in list
		// Neither are fatal problems, so we can ignore.
		cothread_delete(t);
	}
}

constexpr bool Process::all_threads_joined()
{
	for (const auto thread : threads)
	{
		if (thread <= 0)
			continue;

		errno = 0;
		if (cothread_has_joined(thread))
			continue;

		// Detached threads are removed by libnds as soon as they finish.
		if (errno == EINVAL)
			continue;

		return false;
	}
	return true;
}

static std::list<Process> processes{};
static Process &kernel_process = processes.emplace_back();
static int global_pid_counter = 0;

static pid_t normalize_process_group_id(pid_t pid, pid_t pgid)
{
	return (pgid == 0) ? pid : pgid;
}

void set_kernel_process()
{
	kernel_process.pid = 0;
	kernel_process.ppid = -1;
	kernel_process.pgid = 0;
	kernel_process.threads.emplace_back(cothread_get_current());
	kernel_process.fdtable[0] = 0;
	kernel_process.fdtable[1] = 1;
	kernel_process.fdtable[2] = 2;
}

Process &get_current_process()
{
	const auto process = get_process_by_thread(cothread_get_current());
	if (process)
		return *process;
	puts("kernel: current process not found! crashing");
	libndsCrash("current process not found");
}

Process *get_process_by_thread(cothread_t thread)
{
	for (auto &p : processes)
		if (const auto itr{std::ranges::find(p.threads, thread)}; itr != p.threads.end())
			return &p;
	printf("kernel: no process for thread %d\n", thread);
	return {};
}

int set_process_group(pid_t pid, pid_t pgid)
{
	auto &current = get_current_process();
	if (pid == 0)
		pid = current.pid;

	auto *process = get_process(pid);
	if (!process)
	{
		errno = ESRCH;
		return -1;
	}

	if (pgid < 0)
	{
		errno = EINVAL;
		return -1;
	}

	const auto current_pid = current.pid;
	if (process->pid != current_pid && process->ppid != current_pid)
	{
		errno = EPERM;
		return -1;
	}

	process->pgid = normalize_process_group_id(process->pid, pgid);
	return 0;
}

int kill_process_group(pid_t pgid, int sig)
{
	if (sig <= 0 || sig >= NSIG)
	{
		errno = EINVAL;
		return -1;
	}

	auto &current = get_current_process();
	if (pgid == 0)
		pgid = current.pgid;

	if (pgid <= 0)
	{
		errno = ESRCH;
		return -1;
	}

	bool found = false;
	for (auto &process : processes)
	{
		if (process.pgid != pgid)
			continue;

		found = true;
		process.exit_code = 128 + sig;
		process.status = sig & 0x7f;

		for (const auto thread : process.threads)
		{
			if (thread == cothread_get_current())
				continue;
			cothread_delete(thread);
		}
	}

	if (!found)
	{
		errno = ESRCH;
		return -1;
	}

	return 0;
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

static int process_start_trampoline(void *arg)
{
	auto &p = *(Process *)arg;
	p.exit_code = p.entrypoint(p.argv.count(), p.argv.data, p.envp.data);

	// No mechanism for anything other than normal process exits exist, so that's all we will store.
	p.status = (p.exit_code << 8) | 0x00;

	// Save `environ` before we implicitly yield and die, so our destructor can properly free it
	// if we reallocated it via setenv(). See the end of cothread_yield() for more info.
	extern char **environ;
	p.envp.data = environ;

	// printf("thread %d exiting\n", cothread_get_current());
	return p.exit_code;
}

extern "C" int posix_spawn(
	pid_t *pid,
	const char *path,
	const posix_spawn_file_actions_t *file_actions,
	const posix_spawnattr_t *attrp,
	char *const argv[],
	char *const envp[])
{
	auto &parent = get_current_process();
	(void)file_actions;
	(void)attrp;

	const auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle)
	{
		printf("kernel: dlopen: %s\n", dlerror());
		errno = ENOEXEC;
		return -1;
	}

	const auto entrypoint = (Process::MainFn)dlsym(handle, "main");
	if (!entrypoint)
	{
		printf("dlsym: %s\n", dlerror());
		dlclose(handle);
		errno = ENOEXEC;
		return -1;
	}

	// Create the process object first, then create the thread with a stable address.
	auto &child = processes.emplace_back();
	child.dlhandle = handle;
	child.pid = ++global_pid_counter;
	child.ppid = parent.pid;
	child.pgid = parent.pgid;
	child.entrypoint = entrypoint;

	if (argv)
	{
		child.argv = CStrArray(argv);
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!child.argv.data)
		{
			errno = ENOMEM;
			dlclose(handle);
			processes.erase(get_process_itr(child.pid));
			return -1;
		}
	}

	if (envp)
	{
		child.envp = CStrArray(envp);
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!child.envp.data)
		{
			errno = ENOMEM;
			dlclose(handle);
			processes.erase(get_process_itr(child.pid));
			return -1;
		}
	}

	child.fdtable[0] = 0;
	child.fdtable[1] = 1;
	child.fdtable[2] = 2;

	// Attempt to create the process's first thread
	const auto thread = cothread_create(process_start_trampoline, &child, 0, COTHREAD_DETACHED);
	if (thread < 0)
	{
		// errno is set by cothread_create
		dlclose(handle);
		processes.erase(get_process_itr(child.pid));
		return -1;
	}
	child.threads.emplace_back(thread);

	// Our `cothread_create` override always adds the new thread to the *current process*!
	// Remove it as it actually belongs to the child process.
	parent.threads.pop_back();

	if (pid)
		*pid = child.pid;

	// printf("spawn: pid=%d thread=%d\n", *pid, thread);
	return 0;
}

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
	const auto &parent = get_current_process();
	const auto parent_pid = parent.pid;
	const auto parent_pgid = parent.pgid;

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

	const bool nohang = (options & WNOHANG) != 0;

	for (;;)
	{
		bool has_matching_child = false;

		for (auto it = processes.begin(); it != processes.end(); ++it)
		{
			auto &p = *it;
			if (p.ppid != parent_pid)
				continue;

			bool matches = false;
			if (pid == -1)
				matches = true;
			else if (pid > 0)
				matches = (p.pid == pid);
			else if (pid == 0)
				matches = (p.pgid == parent_pgid);
			else
				matches = (p.pgid == -pid);

			if (!matches)
				continue;

			has_matching_child = true;

			if (!p.all_threads_joined())
				continue;

			const auto child_pid = p.pid;

			if (stat_loc)
				*stat_loc = p.status;

			// Consume the child's wait status (reap).
			// printf("waitpid: reaping %d\n", child_pid);
			it = processes.erase(it);
			// printf("waitpid: reaped %d\n", child_pid);
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

		// printf("waitpid: %d yielding\n", parent_pid);
		cothread_yield();
	}
}
