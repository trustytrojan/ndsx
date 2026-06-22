#include <format>
#include <process_manager.hpp>

#include <dlfcn.h>
#include <fcntl.h>

int process_start_trampoline(void *arg);
extern "C" typeof(close) libnds_close;

extern "C" int execve(const char *path, char *const argv[], char *const envp[])
{
	Process &current = get_current_process();

	// Check if we are a vfork child running on a borrowed thread
	auto parent = get_process(current.ppid);
	if (!parent)
	{
		// The only case in which this can happen is the kernel process itself calling _exit()...
		puts("execve: current process has no parent");
		libndsCrash("current process has no parent");
	}

	if (!parent->is_vfork_suspended)
	{
		// We do not support execve() from a process whose parent is not vfork-suspended.
		// This would involve freeing all of the heap memory of the current process,
		// and although with dlmalloc's mspaces it is possible, it is currently not a priority.
		// We just want enough for the dash shell to be able to execute programs.
		errno = ENOSYS;
		return -1;
	}

	// Setup the current process for the new executable before doing anything vfork-specific.
	// Remember, current IS the child that is going to start running a different program.
	// It has already been setup as a separate process. We just do the rest of the steps as in posix_spawn() here.

	// TODO: replace with DSL caching system from nds-shell
	const auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle)
	{
		printf("dlopen: %s\n", dlerror());
		errno = ENOEXEC;
		return -1;
	}

	current.dlhandle.reset(
		handle,
		[](auto ptr)
		{
			if (ptr && dlclose(ptr) < 0)
				printf("dlclose: %s\n", dlerror());
		});

	const auto entrypoint = (Process::MainFn)dlsym(handle, "main");
	if (!entrypoint)
	{
		printf("dlsym: %s\n", dlerror());
		errno = ENOEXEC;
		return -1;
	}

	current.entrypoint = entrypoint;

	// Remember, current IS the child that is going to start running a different program
	if (argv)
	{
		current.argv.name = std::format("{},{}", current.pid, "argv");
		current.argv = CStrArray(argv, "tmp:" + std::format("{},{}", current.pid, "argv"));
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!current.argv.data)
		{
			errno = ENOMEM;
			return -1;
		}
	}

	if (envp)
	{
		current.envp.name = std::format("{},{}", current.pid, "envp");
		current.envp = CStrArray(envp, "tmp:" + std::format("{},{}", current.pid, "envp"));
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!current.envp.data)
		{
			errno = ENOMEM;
			return -1;
		}
		// FIX: Update the global environ so transfer_current_thread
		// saves the correct pointer later!
		extern char **environ;
		environ = current.envp.data;
		puts("after envp assignment");
	}

	// before passing this thread back to the parent, spawn a new thread for the child
	// that starts at the main() of a dsl.

	// Remember, current IS the child that is going to start running a different program
	const auto thread = cothread_create(process_start_trampoline, &current, 8192, COTHREAD_DETACHED);
	if (thread < 0)
	{
		// errno is set by cothread_create
		return -1;
	}
	// printf("execve: new thread: %d\n", thread);

	// Remove all threads that aren't the newly created one.
	// Remember our cothread_create() override adds the thread to the current process.
	for (auto it = current.threads.begin(); it != current.threads.end(); ++it)
	{
		if (*it == thread)
			continue;
		current.threads.erase(it);
	}

	// Close all FDs marked with FD_CLOEXEC
	for (int i = 0; i < Process::MAX_FDS; ++i)
	{
		if (current.fdflags[i] & FD_CLOEXEC)
		{
			const auto kernel_fd = current.fdtable[i];
			if (kernel_fd > STDERR_FILENO && libnds_close(kernel_fd) == -1)
				perror("libnds_close");
			current.fdtable[i] = -1;
		}
	}

	{
		nds_critical_section cs;
		transfer_current_thread(current, *parent);
	}

	if (get_current_process() != *parent)
	{
		puts("execve: get_current_process() != *parent");
		libndsCrash("execve: get_current_process() != *parent");
	}

	puts("execve: about to longjmp");

	// 4. Time travel! Warp the CPU back into the parent's vfork() call.
	// We pass current.pid, which causes setjmp in vfork() to return the child's PID.
	longjmp(parent->vfork_env, current.pid);
}
