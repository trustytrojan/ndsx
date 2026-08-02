#pragma once

#include <signal.h>
#include <sys/cdefs.h>
#include <sys/types.h>

// poll.h redefines `struct pollfd`, and picolibc doesn't implement poll().
// dswifi/lwip does, so use their header instead.
#include <netdb.h>

_BEGIN_STD_C

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask);

_END_STD_C
