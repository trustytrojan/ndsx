#include <nds/cothread.h>
#include <process_manager.hpp>

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>

#include <errno.h>
#include <list>
#include <sys/_wait.h>

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

static constexpr bool is_valid_signal_number(const int sig)
{
	return sig > 0 && sig < NSIG;
}

static constexpr bool is_unblockable_signal(const int sig)
{
	return sig == SIGKILL || sig == SIGSTOP;
}

static constexpr bool is_default_ignored_signal(const int sig)
{
	return sig == SIGCHLD;
}

static void sanitize_signal_mask(sigset_t &mask)
{
	// POSIX requires SIGKILL and SIGSTOP to be unblocked silently.
	sigdelset(&mask, SIGKILL);
	sigdelset(&mask, SIGSTOP);
}

static bool signal_is_blocked(const Process &process, const int sig)
{
	return sigismember(&process.signal_mask, sig) == 1;
}

static void queue_signal(Process &process, const int sig)
{
	sigaddset(&process.pending_signals, sig);
}

static struct sigaction default_sigaction()
{
	struct sigaction action{};
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	return action;
}

static struct sigaction get_installed_sigaction(Process &process, const int sig)
{
	auto action = process.signal_actions[sig];
	if (action.sa_handler == nullptr)
		action = default_sigaction();
	return action;
}

static void set_default_sigaction(Process &process, const int sig)
{
	process.signal_actions[sig] = default_sigaction();
}

enum class SignalDeliveryResult
{
	None,
	Caught,
	Terminated,
};

static SignalDeliveryResult deliver_signal(Process &process, const int sig)
{
	sigdelset(&process.pending_signals, sig);
	const auto action = get_installed_sigaction(process, sig);

	if (action.sa_handler == SIG_IGN)
		return SignalDeliveryResult::None;

	if (action.sa_handler == SIG_DFL)
	{
		if (is_default_ignored_signal(sig))
			return SignalDeliveryResult::None;

		process.exit_code = 128 + sig;
		process.status = sig & 0x7f;

		for (const auto thread : process.threads)
		{
			if (thread == cothread_get_current())
				continue;
			cothread_delete(thread);
		}

		return SignalDeliveryResult::Terminated;
	}

	sigset_t old_mask = process.signal_mask;
	sigset_t handler_mask = old_mask;

	for (int blocked_sig = 1; blocked_sig < NSIG; ++blocked_sig)
	{
		if (sigismember(&action.sa_mask, blocked_sig) == 1)
			sigaddset(&handler_mask, blocked_sig);
	}

	if ((action.sa_flags & SA_NODEFER) == 0)
		sigaddset(&handler_mask, sig);

	sanitize_signal_mask(handler_mask);
	process.signal_mask = handler_mask;

	if (action.sa_flags & SA_RESETHAND)
		set_default_sigaction(process, sig);

	if (action.sa_flags & SA_SIGINFO)
		action.sa_sigaction(sig, nullptr, nullptr);
	else
		action.sa_handler(sig);

	process.signal_mask = old_mask;
	return SignalDeliveryResult::Caught;
}

bool deliver_pending_signals(Process &process, bool *caught_signal)
{
	if (caught_signal)
		*caught_signal = false;

	for (int sig = 1; sig < NSIG; ++sig)
	{
		if (sigismember(&process.pending_signals, sig) != 1)
			continue;
		if (signal_is_blocked(process, sig))
			continue;

		switch (deliver_signal(process, sig))
		{
			case SignalDeliveryResult::None:
				continue;
			case SignalDeliveryResult::Caught:
				if (caught_signal)
					*caught_signal = true;
				return true;
			case SignalDeliveryResult::Terminated:
				return true;
		}
	}

	return false;
}

static constexpr pid_t normalize_process_group_id(pid_t pid, pid_t pgid)
{
	return (pgid == 0) ? pid : pgid;
}

void set_kernel_process()
{
	kernel_process.pid = 0;
	kernel_process.ppid = -1;
	kernel_process.pgid = 0;
	sigemptyset(&kernel_process.signal_mask);
	sigemptyset(&kernel_process.pending_signals);
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

// Syscalls that need to modify process state are below; others are in src/kernel/syscalls/process.cpp.
extern "C"
{
int posix_spawn(
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
	sigemptyset(&child.signal_mask);
	sigemptyset(&child.pending_signals);

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

pid_t waitpid(pid_t pid, int *stat_loc, int options)
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

int setpgid(pid_t pid, pid_t pgid)
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

int killpg(pid_t pgid, int sig)
{
	if (!is_valid_signal_number(sig))
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
		queue_signal(process, sig);
	}

	if (!found)
	{
		errno = ESRCH;
		return -1;
	}

	// Allow synchronous self-delivery semantics for the calling thread.
	deliver_pending_signals(current);

	return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	if (!is_valid_signal_number(sig))
	{
		errno = EINVAL;
		return -1;
	}

	auto &process = get_current_process();
	if (oact)
		*oact = get_installed_sigaction(process, sig);

	if (!act)
		return 0;

	if (is_unblockable_signal(sig))
	{
		errno = EINVAL;
		return -1;
	}

	process.signal_actions[sig] = *act;
	sanitize_signal_mask(process.signal_actions[sig].sa_mask);
	return 0;
}

int sigsuspend(const sigset_t *mask)
{
	auto &process = get_current_process();
	const auto old_mask = process.signal_mask;

	process.signal_mask = *mask;
	sanitize_signal_mask(process.signal_mask);

	for (;;)
	{
		bool caught = false;
		if (deliver_pending_signals(process, &caught))
		{
			process.signal_mask = old_mask;
			if (caught)
			{
				errno = EINTR;
				return -1;
			}
		}

		cothread_yield();
	}
}
}
