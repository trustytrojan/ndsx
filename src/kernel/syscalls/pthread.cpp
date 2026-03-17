#include <cerrno>
#include <process_manager.hpp>
#include <pthread.h>

extern "C"
{
int pthread_create(pthread_t *thread, const pthread_attr_t *, typeof(void *(void *)) *start_routine, void *arg)
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
		cothread_yield();
	const int rc = cothread_get_exit_code(thread);
	if (rc == -1)
		return errno;
	if (retval != NULL)
		*retval = (void *)rc;
	return 0;
}

int pthread_yield(void)
{
	extern char **environ;
	const auto next_thread = (cothread_t)((cothread_info_t *)cothread_get_current())->next;
	if (next_thread)
	{
		// printf("yield: nt=%d", next_thread);
		if (const auto next_process = get_process_by_thread(next_thread))
		{
			environ = next_process->envp.data;
			// printf(" np=%d env=%d", next_process->pid, (bool)environ);
		}
		// putchar('\n');
	}
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
