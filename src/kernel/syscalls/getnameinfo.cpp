#include <netdb.h>
#include <netinet/in.h>
#include <sys/getnameinfo.h>
#include <sys/socket.h>

#include <cerrno>
#include <charconv>
#include <span>
#include <system_error>

extern "C" int getnameinfo(
	const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags)
{
	(void)salen;

	// POSIX Spec: At least one of host or serv must be requested
	if ((!host || hostlen == 0) && (!serv || servlen == 0)) [[unlikely]]
		return EAI_NONAME;

	if (!sa) [[unlikely]]
		return EAI_FAMILY;

	// Since reverse DNS resolution isn't supported, if a named resolution is
	// strictly required (and numeric host isn't set via NI_NUMERICHOST), fail.
	if ((flags & NI_NAMEREQD) && !(flags & NI_NUMERICHOST)) [[unlikely]]
		return EAI_NONAME;

	std::span<char> host_buf{host, host ? hostlen : 0};
	std::span<char> serv_buf{serv, serv ? servlen : 0};

	// 1. Format Host IP Address using inet_ntop for IPv4 and IPv6
	if (!host_buf.empty())
	{
		const void *addr_ptr = nullptr;

		if (sa->sa_family == AF_INET)
			addr_ptr = &reinterpret_cast<const struct sockaddr_in *>(sa)->sin_addr;
		else if (sa->sa_family == AF_INET6)
			addr_ptr = &reinterpret_cast<const struct sockaddr_in6 *>(sa)->sin6_addr;
		else
			return EAI_FAMILY;

		if (!inet_ntop(sa->sa_family, addr_ptr, host_buf.data(), host_buf.size()))
		{
			if (errno == ENOSPC)
				return EAI_MEMORY; // Map buffer overflow to EAI_MEMORY
			return EAI_FAIL;
		}
	}

	// 2. Format Port Number
	if (!serv_buf.empty())
	{
		uint16_t port = 0;

		if (sa->sa_family == AF_INET)
			port = ntohs(reinterpret_cast<const struct sockaddr_in *>(sa)->sin_port);
		else if (sa->sa_family == AF_INET6)
			port = ntohs(reinterpret_cast<const struct sockaddr_in6 *>(sa)->sin6_port);
		else
			return EAI_FAMILY;

		auto [ptr, ec] = std::to_chars(serv_buf.data(), serv_buf.data() + serv_buf.size() - 1, port);
		if (ec == std::errc::value_too_large) [[unlikely]]
			return EAI_MEMORY;
		else if (ec != std::errc{}) [[unlikely]]
			return EAI_FAIL;
		*ptr = '\0';
	}

	return 0; // Success
}
