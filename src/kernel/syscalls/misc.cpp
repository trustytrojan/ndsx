#include <cerrno>
#include <cstring>

#include <netdb.h>
#include <sched.h>
#include <sys/cpuset.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <nds.h>

extern "C"
{
const char *hstrerror(int err)
{
	switch (err)
	{
	/* Resolver error codes (h_errno) */
	case HOST_NOT_FOUND:
		return "Unknown host";
	case NO_DATA:
		return "No address associated with name";
	case NO_RECOVERY:
		return "Non-recoverable failure in name resolution";
	case TRY_AGAIN:
		return "Temporary failure in name resolution";

	/* Extended address info error codes (EAI) */
	case EAI_NONAME:
		return "Name or service not known";
	case EAI_SERVICE:
		return "Servname not supported for ai_socktype";
	case EAI_FAIL:
		return "Non-recoverable failure in name resolution";
	case EAI_MEMORY:
		return "Memory allocation failure";
	case EAI_FAMILY:
		return "ai_family not supported";

	default:
		return "Unknown resolver error";
	}
}

int getgroups(int gidsetsize, gid_t grouplist[])
{
	// POSIX requirement: If gidsetsize is negative, it's an invalid argument
	if (gidsetsize < 0)
	{
		errno = EINVAL;
		return -1;
	}

	// POSIX requirement: If gidsetsize is 0, return the number of
	// supplementary group IDs without modifying the grouplist array.
	if (gidsetsize == 0)
		return 0;

	// If gidsetsize > 0, we would normally fill grouplist.
	// Since we have 0 groups, we write nothing to the array and return 0.
	return 0;
}

mode_t umask(mode_t cmask)
{
	return 0;
}

int getrlimit(int resource, struct rlimit *rlim)
{
	(void)resource; // Suppress unused parameter warning
	if (rlim)
	{
		rlim->rlim_cur = INT32_MAX;
		rlim->rlim_max = INT32_MAX;
	}
	return 0;
}

int setrlimit(int, const struct rlimit *)
{
	return 0;
}

int settimeofday(const struct timeval *, const struct timezone *)
{
	errno = ENOSYS;
	return -1;
}

int setsid(void)
{
	errno = ENOSYS;
	return -1;
}

int initgroups(const char *, gid_t)
{
	errno = ENOSYS;
	return -1;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
	errno = ENOSYS;
	return -1;
}

int uname(struct utsname *buf)
{
	if (!buf)
		return -1;

	*buf = {};
	strncpy(buf->sysname, "ndsx", _UTSNAME_LENGTH);
	strncpy(buf->nodename, "localhost", _UTSNAME_LENGTH);
	strncpy(buf->release, "1.0.0", _UTSNAME_LENGTH);
	strncpy(buf->version, "1.0.0", _UTSNAME_LENGTH);
	strncpy(buf->machine, isDSiMode() ? "dsi" : "nds", _UTSNAME_LENGTH);

	return 0;
}

int chroot(const char *path)
{
	errno = ENOSYS;
	return -1;
}

int fchdir(int fildes)
{
	errno = ENOSYS;
	return -1;
}

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	errno = ENOSYS;
	return -1;
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
	errno = ENOSYS;
	return -1;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	errno = ENOSYS;
	return NULL;
}
}
