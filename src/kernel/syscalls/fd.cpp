#include <process_manager.hpp>

#include <cerrno>

// File descriptor system calls.

extern "C"
{
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

	if (kernel_fd == STDIN_FILENO)
	{
		// we can simply handle this here instead of requiring it from libnds
		char *cp = (char *)ptr;
		const char *const end = (char *)ptr + len;
		for (; cp <= end; ++cp)
		{
			int libnds_stdin_getc_keyboard(FILE *);
			const char c = libnds_stdin_getc_keyboard(NULL);
			if (c <= 0)
				// keyboard uninitialized, or other problem
				break;
			*cp = c;
		}
		return cp - (char *)ptr;
	}

	typeof(read) libnds_read;
	return libnds_read(kernel_fd, ptr, len);
}

ssize_t write(int fd, const void *ptr, size_t len)
{
	const auto &p = get_current_process();

	if (fd < 0 || fd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	const auto kernel_fd = p.fdtable[fd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	if (kernel_fd == STDOUT_FILENO || kernel_fd == STDERR_FILENO)
	{
		// we can simply handle this here instead of requiring it from libnds
		extern ConsoleOutFn libnds_stdout_write, libnds_stderr_write;
		const ConsoleOutFn fn = (kernel_fd == STDOUT_FILENO) ? libnds_stdout_write : libnds_stderr_write;
		return fn((char *)ptr, len);
	}

	typeof(write) libnds_write;
	return libnds_write(kernel_fd, ptr, len);
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
