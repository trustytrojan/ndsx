#include "process_manager.hpp"
#include "CStrArray.hpp"

#include <nds/cothread.h>

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <algorithm>
#include <csetjmp>
#include <list>

static std::list<Process> processes{};
static Process &kernel_process{processes.emplace_back()};
static int global_pid_counter{};

bool kernelfd_in_use(int kfd)
{
	return std::ranges::any_of(
		processes, [=](const auto &p) { return std::ranges::any_of(p.fdtable, [=](auto fd) { return fd == kfd; }); });
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
	puts("get_current_process: current process not found! crashing");
	libndsCrash("current process not found");
}

Process *get_process_by_thread(cothread_t thread)
{
	for (auto &p : processes)
		if (const auto itr{std::ranges::find(p.threads, thread)}; itr != p.threads.end())
			return &p;
	printf("get_process_by_thread: no process for thread %d\n", thread);
	return {};
}

auto get_process_itr(pid_t pid)
{
	return std::ranges::find_if(processes, [=](const auto &p) { return p.pid == pid; });
}

Process *get_process(pid_t pid)
{
	const auto itr = get_process_itr(pid);
	return (itr == processes.end()) ? nullptr : &*itr;
}

int process_start_trampoline(void *arg)
{
	extern char **environ;
	auto &p = *(Process *)arg;

	// Restore `environ` in the same way our cothread_yield() override does.
	environ = p.envp.data;

	const auto status = setjmp(p.exit_env);
	if (status == 0)
		p.exit_code = p.entrypoint(p.argv.count(), p.argv.data, p.envp.data);
	else
		p.exit_code = status;

	// No mechanism for anything other than normal process exits exist, so that's all we will store.
	p.status = (p.exit_code << 8) | 0x00;

	// Save `environ` before we implicitly yield and die, so our destructor can properly free it
	// if it was realloc'd via setenv(). See the end of cothread_yield() for more info.
	p.envp.data = environ;

	/* Save this for when you wrap the cothread_(yield|send)_signal functions.
	// Wake up any threads stuck inside waitpid()
	cothread_send_signal(p.pid);
	// Set bit 30 because we need to wake up a waitpid(<=0) call instead of a waitpid(>0) call.
	cothread_send_signal(BIT(30) | p.ppid);
	*/

	return p.exit_code;
}

void transfer_current_thread(Process &from, Process &to)
{
	const auto current_thread = cothread_get_current();
	const auto it = std::ranges::find(from.threads, current_thread);
	if (it != from.threads.end())
		from.threads.erase(it);
	to.threads.emplace_back(current_thread);

	assert(get_current_process() == to);

	extern char **environ;
	from.envp.data = environ;
	environ = to.envp.data;
}

// Syscalls that need to modify process state are below; others are in src/kernel/syscalls/process.cpp.
extern "C"
{
// extern "C" __attribute__((naked)) pid_t vfork()
// {
// 	asm volatile(
// 		"push {r4, lr}\n\t"	  // Save main's return address and align stack to 8 bytes
// 		"bl   vfork_impl\n\t" // Call your C++ function
// 		"pop  {r4, pc}\n\t"	  // Unwind frame and return to main() with r0 intact
// 	);
// }

pid_t vfork()
{
	static thread_local void *return_addr;

	// Save return address to parent's vfork() call, otherwise, on the 2nd return,
	// execution will end up where the child called _exit() or execve().
	return_addr = __builtin_return_address(0);

	// setjmp returns 0 initially. When we longjmp back later, it will return the child PID.
	pid_t ret = setjmp(get_current_process().vfork_env);

	// You might be thinking: wouldn't this become the child when longjmp() jumps here?
	// No, it won't, because _exit() and execve() will transfer the thread back to the
	// parent before calling longjmp().
	auto &parent = get_current_process();

	printf("vfork: setjmp: %d\n", ret);
	printf("vfork: parent: %p\n", &parent);
	printf("vfork: parent pid: %d\n", parent.pid);

	if (ret == 0)
	{
		// **Make a copy** of the Process struct entirely.
		// This also increments the reference counter of dlhandle,
		// so if the child execve()s it needs to swap dlhandle with the new image
		// so that this one's reference count is decremented.
		auto &child = processes.emplace_back(parent);

		// Child-specific changes
		child.pid = ++global_pid_counter;
		child.ppid = parent.pid;
		sigemptyset(&child.pending_signals);
		child.threads.clear();

		{
			nds_critical_section cs;
			transfer_current_thread(parent, child);
			parent.is_vfork_suspended = true;
		}

		sassert(get_current_process() == child, "%d == %d", get_current_process().pid, child.pid);

		puts("vfork: returning 0");

		// return 0 to the new child process
		return 0;
	}
	else
	{
		// _exit() or execve() jumped here, so it has already called transfer_current_thread().
		sassert(get_current_process() == parent, "%d == %d", get_current_process().pid, parent.pid);

		// We just need to mark the parent as no longer suspended by vfork().
		{
			nds_critical_section cs;
			parent.is_vfork_suspended = false;
		}

		printf("vfork: returning %d\n", ret);

		// Restore return address to parent's vfork() call, otherwise execution
		// will end up where the child called _exit() or execve().
		__builtin_eh_return(0, return_addr);

		return ret;
	}
}

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

	// TODO: replace with DSL caching system from nds-shell
	const auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle)
	{
		printf("dlopen: %s\n", dlerror());
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
	child.dlhandle = {
		handle,
		[](auto ptr)
		{
			if (ptr && dlclose(ptr) < 0)
				printf("dlclose: %s\n", dlerror());
		}};
	child.pid = ++global_pid_counter;
	child.ppid = parent.pid;
	child.pgid = parent.pgid;
	child.entrypoint = entrypoint;

	const auto itr = get_process_itr(child.pid);

	if (argv)
	{
		child.argv = CStrArray(argv);
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!child.argv.data)
		{
			errno = ENOMEM;
			processes.erase(itr);
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
			processes.erase(itr);
			return -1;
		}
	}

	child.fdtable[0] = 0;
	child.fdtable[1] = 1;
	child.fdtable[2] = 2;
	sigemptyset(&child.signal_mask);
	sigemptyset(&child.pending_signals);

	// Create the child's first thread.
	// You definitely want a larger stack for complex programs, like shells.
	// It might be a good idea to follow the traditional kernel pattern of giving
	// the main thread of a process more stack memory than its child threads.
	const auto thread = cothread_create(process_start_trampoline, &child, 4096, COTHREAD_DETACHED);
	if (thread < 0)
	{
		// errno is set by cothread_create
		processes.erase(itr);
		return -1;
	}
	child.threads.assign({thread});

	if (pid)
		*pid = child.pid;

	return 0;
}

pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
	const auto &parent = get_current_process();

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

	const bool nohang = (options & WNOHANG);

	for (;;)
	{
		bool has_matching_child = false;

		for (auto &child : processes)
		{
			// p must be a child of parent
			if (child.ppid != parent.pid)
				continue;

			bool matches = false;
			if (pid == -1)
				// Match any child
				matches = true;
			else if (pid > 0)
				// Match a specific child
				matches = (child.pid == pid);
			else if (pid == 0)
				// Match any child in the same process group
				matches = (child.pgid == parent.pgid);
			else if (pid < 0)
				// Match any child in a specific process group
				matches = (child.pgid == -pid);
			// else: should not be possible to reach

			if (!matches)
				continue;

			has_matching_child = true;

			if (!child.all_threads_joined())
				continue;

			// printf("waitpid: pid %d: all %d threads joined\n", p.pid, p.threads.size());

			// Have PID 0 (the libnds main thread) adopt any orphaned processes,
			// since all it does is sit in a wait() loop. We don't want PID 0 to
			// to leave that loop unless all child processes have exited, otherwise
			// it will call cothread_yield_irq() forever, which still lets other
			// threads (and therefore processes) run.
			for (auto &orphan : processes)
			{
				if (orphan.ppid != child.pid)
					continue;
				orphan.ppid = 0;
			}

			if (stat_loc)
				*stat_loc = child.status;

			const auto child_pid = child.pid;

			// puts("waitpid: before processes.remove(p)");
			processes.remove(child);
			// puts("waitpid: after processes.remove(p)");

			return child_pid;
		}

		if (!has_matching_child)
		{
			// puts("waitpid: !has_matching_child");
			errno = ECHILD; // No children
			return -1;
		}

		if (nohang)
			return 0;

		cothread_yield();

		/* Save this for when you wrap the cothread_(yield|send)_signal functions.
		// Sleep until the specific target child (pid > 0) or any child (pid <= 0) owned by
		// this parent signals exit (aka sends the same signal in process_start_trampoline).
		if (pid > 0)
			cothread_yield_signal(pid);
		else
			// Set bit 30 because we need to distinguish this waitpid(<=0) call from a waitpid(>0) call.
			cothread_yield_signal(BIT(30) | parent.pid);
		*/
	}
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
}
