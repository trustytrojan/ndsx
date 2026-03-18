#include <cerrno>
#include <process_manager.hpp>
#include <pthread.h>

/*
As with the rest of this project, this is a weird hybrid of user-space and kernel-space code.

Since process creation requires recording the new thread in the new process, `posix_spawn` needs to
use `cothread_create` directly. But for us to keep control of userspace, userspace should only call
`pthread_create`, which adds the newly created thread to the current process.

We can enforce this with my symbol resolver branch of libnds, once I write userspace threading tests.
*/

extern "C"
{
int pthread_create(pthread_t *thread, const pthread_attr_t *, void *(*start_routine)(void *), void *arg)
{
	const auto thr = cothread_create((cothread_entrypoint_t)start_routine, arg, 0, 0);
	if (thr < 0)
		return errno;
	*thread = thr;
	get_current_process().threads.emplace_back(thr);
	return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
	while (!cothread_has_joined(thread))
		pthread_yield();
	const int rc = cothread_get_exit_code(thread);
	if (rc == -1)
		return errno;
	if (retval != NULL)
		*retval = (void *)rc;
	return 0;
}

int pthread_yield(void)
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
	cothread_yield();

	return 0;
}

int pthread_detach(pthread_t thread)
{
	return (cothread_detach(thread) < 0) ? errno : 0;
}

pthread_t pthread_self(void)
{
	return cothread_get_current();
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1 == t2;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *)
{
	comutex_init(mutex);
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	comutex_acquire(mutex);
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return comutex_try_acquire(mutex) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	comutex_release(mutex);
	return 0;
}
}
