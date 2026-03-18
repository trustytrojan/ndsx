#pragma once

#include <nds/cothread.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PTHREAD_THREADS_MAX 8

typedef cothread_t pthread_t;
typedef void pthread_attr_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_yield(void);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);

typedef comutex_t pthread_mutex_t;
typedef void pthread_mutexattr_t;

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

#ifdef __cplusplus
}
#endif
