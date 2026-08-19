#pragma once

#include <sys/cdefs.h>
#include <sys/socket.h>

_BEGIN_STD_C

/* Flags for getnameinfo() */
#ifndef NI_NUMERICHOST
	#define NI_NUMERICHOST 0x01 /* Return numeric form of the host's address */
#endif
#ifndef NI_NUMERICSERV
	#define NI_NUMERICSERV 0x02 /* Return numeric form of the service port */
#endif
#ifndef NI_NOFQDN
	#define NI_NOFQDN 0x04 /* Return only the hostname part of FQDN for local hosts */
#endif
#ifndef NI_NAMEREQD
	#define NI_NAMEREQD 0x08 /* Return an error if the hostname cannot be resolved */
#endif
#ifndef NI_DGRAM
	#define NI_DGRAM 0x10 /* Service is datagram (UDP) based */
#endif

/* Recommended buffer sizes per POSIX spec */
#ifndef NI_MAXHOST
	#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
	#define NI_MAXSERV 32
#endif

int getnameinfo(
	const struct sockaddr *sa,
	socklen_t salen,
	char *host,
	socklen_t hostlen,
	char *serv,
	socklen_t servlen,
	int flags);

_END_STD_C
