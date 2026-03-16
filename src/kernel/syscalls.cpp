#include "process_manager.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio-posix.h>
#include <sys/unistd.h>

#include <cerrno>

extern "C"
{
pid_t getpid()
{
	return get_current_process().pid;
}

// since this isn't defined in libnds, we can define it directly, and don't need to put it in `syscall_overrides`.
pid_t getppid()
{
	return get_current_process().ppid;
}

int open(const char *path, int flags, ...)
{
	auto &p = get_current_process();

	// find first open slot
	int i = 0;
	for (; i < Process::MAX_FDS && p.fdtable[i] >= 0; ++i)
		;

	if (i >= Process::MAX_FDS)
	{
		errno = ENFILE;
		return -1;
	}

	typeof(open) libnds_open;
	const auto kernel_fd = libnds_open(path, flags);

	if (kernel_fd == -1)
		return -1;

	// map USER fd `i` to KERNEL fd `kernel_fd`
	p.fdtable[i] = kernel_fd;

	// we return the USER fd, not the KERNEL fd which should be private to us
	return i;
}

int close(int fd)
{
	if (fd < 0 || fd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	auto &p = get_current_process();

	const auto kernel_fd = p.fdtable[fd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	// libnds close() does not operate on standard streams, so let's handle that first
	if (kernel_fd >= STDIN_FILENO && kernel_fd <= STDERR_FILENO)
	{
		p.fdtable[fd] = -1;
		return 0;
	}

	typeof(close) libnds_close;
	if (libnds_close(kernel_fd) == -1)
		return -1;

	// mark slot as open
	p.fdtable[fd] = -1;

	return 0;
}

ssize_t read(int fd, void *ptr, size_t len)
{
	if (fd < 0 || fd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	const auto &p = get_current_process();

	const auto kernel_fd = p.fdtable[fd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	typeof(read) libnds_read;
	return libnds_read(kernel_fd, ptr, len);
}

ssize_t write(int fd, const void *ptr, size_t len)
{
	const auto &p = get_current_process();

	extern ConsoleOutFn libnds_stderr_write;
	static char buf[100] = {};


	if (fd < 0 || fd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	const auto kernel_fd = p.fdtable[fd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		int _len = snprintf(buf, sizeof(buf) - 1, "kernel: write: %s\n", strerror(errno));
		libnds_stderr_write(buf, _len);
		return -1;
	}

	typeof(write) libnds_write;
	const auto rc = libnds_write(kernel_fd, ptr, len);
	if (rc == -1)
	{
		int _len = snprintf(buf, sizeof(buf) - 1, "kernel: write: %s\n", strerror(errno));
		libnds_stderr_write(buf, _len);
	}
	return rc;
	// return libnds_write(kernel_fd, ptr, len);
}

off_t lseek(int fd, off_t offset, int whence)
{
	if (fd < 0 || fd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	const auto &p = get_current_process();

	const auto kernel_fd = p.fdtable[fd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	typeof(lseek) libnds_lseek;
	return libnds_lseek(kernel_fd, offset, whence);
}
}
