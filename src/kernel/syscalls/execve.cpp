// #include <format>
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
		// The only case in which this can happen is the kernel process itself calling execve()...
		puts("execve: called by pid 0\npress START to exit");
		while (true)
		{
			scanKeys();
			if (keysDown() & KEY_START)
			{
				typeof(_exit) libnds__exit;
				libnds__exit(-1);
			}
		}
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
		// current.argv.name = std::format("{},{}", current.pid, "argv");
		// current.argv = CStrArray(argv, "tmp:" + std::format("{},{}", current.pid, "argv"));
		current.argv = CStrArray(argv);
		// The constructor does a deep-copy, so if it's still empty, memory failed to allocate.
		if (!current.argv.data)
		{
			errno = ENOMEM;
			return -1;
		}
	}

	if (envp)
	{
		// current.envp.name = std::format("{},{}", current.pid, "envp");
		// current.envp = CStrArray(envp, "tmp:" + std::format("{},{}", current.pid, "envp"));
		current.envp = CStrArray(envp);
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
	}

	// Before passing this thread back to the parent, spawn a new thread for the child
	// that starts at the main() of a dsl.

	// Remember, `current` is the child that is going to start running the loaded program.
	// You definitely want a larger stack for complex programs, like a POSIX-compliant shell.
	const auto thread = cothread_create(process_start_trampoline, &current, 4096, COTHREAD_DETACHED);
	if (thread < 0)
	{
		// errno is set by cothread_create
		return -1;
	}

	// Remove all threads that aren't the newly created one.
	// Remember our cothread_create() override adds the thread to the current process.
	current.threads.assign({thread});

	// "Close" all FDs marked with FD_CLOEXEC. We do NOT want to close the actual resources
	// with libnds_close() because they belong to the parent.
	for (int i = 0; i < Process::MAX_FDS; ++i)
		if (current.fdflags[i] & FD_CLOEXEC)
			current.fdtable[i] = -1;

	{ // This NEEDS to be here, otherwise process bookkeeping breaks.
		nds_critical_section cs;
		transfer_current_thread(current, *parent);
		// transfer_current_thread asserts that the current process is now *parent.
	}

	// puts("execve: about to longjmp");

	// Longjmp back to vfork() as the parent process.
	longjmp(parent->vfork_env, current.pid);
}
