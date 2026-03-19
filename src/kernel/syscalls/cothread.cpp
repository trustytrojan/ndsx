#include <process_manager.hpp>

/* libnds cothreading API overrides */

extern "C"
{
cothread_t cothread_create(cothread_entrypoint_t entrypoint, void *arg, size_t stack_size, unsigned int flags)
{
	typeof(cothread_create) libnds_cothread_create;
	const auto thread = libnds_cothread_create(entrypoint, arg, stack_size, flags);
	if (thread < 0)
		// errno is set
		return -1;
	get_current_process().threads.emplace_back(thread);
	return thread;
}

void cothread_yield(void)
{
	// Before yielding, let's swap out libc's `environ` value with the next process's `envp`.
	// This lets processes have fully isolated environments!
	extern char **environ;

	// Luckily, libnds makes `cothread_t` just a cast of `cothread_info_t *`, which is publicly defined.
	// The only case in which `next` is NULL is when this thread is the last in libnds's list.
	// You would think this is a problem, but since the first thread is the libnds main() thread
	// (which libnds will wrap around to when `next` is NULL), it's the kernel process, which doesn't
	// need an environment anyway.
	if (const auto next_thread = (cothread_t)((cothread_info_t *)cothread_get_current())->next)
	{
		// Save `environ` to the current process.
		// It may have called setenv() with a new key, which causes a realloc() on `environ`.
		get_current_process().envp.data = environ;

		// printf("yield: nt=%d", next_thread);
		if (const auto next_process = get_process_by_thread(next_thread))
		{
			// Even if the next thread is in the same process, this is OK.
			// `environ` is not thread-local. If it was, we'd have more problems.
			environ = next_process->envp.data;
			// printf(" np=%d env=%d", next_process->pid, (bool)environ);
		}
		else
		{
			// There is a bug in process-thread bookkeeping!
			fputs("kernel: no process for next thread! crashing\n", stderr);
			libndsCrash("no process for next thread");
		}
		// putchar('\n');
	}

	// Now we actually yield to the next thread.
	void libnds_cothread_yield();
	libnds_cothread_yield();

	// When we resume, we might have been yielded to implicitly by a dying thread,
	// which doesn't call our wrapper and therefore leaves `environ` pointing to its own environment!
	// We MUST restore `environ` to our environment to prevent claiming ownership of a dead thread's environment.
	environ = get_current_process().envp.data;
}
}
