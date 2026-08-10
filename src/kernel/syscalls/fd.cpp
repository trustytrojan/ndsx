#include "pipe_ops.hpp"
#include <process_manager.hpp>

#include <cerrno>
#include <cstdarg>
#include <cstring>

#include <fcntl.h>
#include <sys/_default_fcntl.h>

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
	// initialize user-visible fd flags (clear FD_CLOEXEC)
	p.fdflags[i] = 0;

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

	// mark slot as open, we are definitely closing the kernel fd
	p.fdtable[fd] = -1;
	printf("p.fdtable[%d] = %d\n", fd, p.fdtable[fd]);

	if (_pipe::is_kfd(kernel_fd))
		return _pipe::close(kernel_fd);

	typeof(close) libnds_close;
	return libnds_close(kernel_fd);
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

	// pipe read
	if (_pipe::is_kfd(kernel_fd))
		return _pipe::read(kernel_fd, ptr, len, p.fdstatus[fd] & O_NONBLOCK);

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

	if (_pipe::is_kfd(kernel_fd))
		return _pipe::write(kernel_fd, ptr, len, p.fdstatus[fd] & O_NONBLOCK);

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

int dup(int oldfd)
{
	if (oldfd < 0 || oldfd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	auto &p = get_current_process();

	// get existing KERNEL fd
	const auto kernel_fd = p.fdtable[oldfd];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	// find first open slot
	int i = 0;
	for (; i < Process::MAX_FDS && p.fdtable[i] >= 0; ++i)
		;

	if (i >= Process::MAX_FDS)
	{
		errno = ENFILE;
		return -1;
	}

	// map USER fd `i` to KERNEL fd `kernel_fd`
	p.fdtable[i] = kernel_fd;

	// we return the USER fd, not the KERNEL fd which should be private to us
	// duplicate flags: new descriptor starts with FD_CLOEXEC cleared per F_DUPFD semantics
	p.fdflags[i] = 0;
	p.fdstatus[i] = p.fdstatus[oldfd]; // Track fdstatus in dup
	return i;
}

int dup2(int oldfd, int newfd)
{
	if (oldfd < 0 || oldfd >= Process::MAX_FDS || newfd < 0 || newfd >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	if (oldfd == newfd)
		return 0;

	auto &p = get_current_process();

	// get existing KERNEL fd
	const auto kernel_oldfd = p.fdtable[oldfd];
	if (kernel_oldfd < 0)
	{
		errno = EBADF;
		return -1;
	}

	// get target KERNEL fd, close it if in use
	const auto kernel_newfd = p.fdtable[newfd];
	if (kernel_newfd >= 0)
	{
		typeof(close) libnds_close;
		libnds_close(kernel_newfd);
	}

	// map newfd to same KERNEL fd as oldfd
	p.fdtable[newfd] = kernel_oldfd;

	// preserve flags from oldfd for dup2 semantics
	p.fdflags[newfd] = p.fdflags[oldfd];
	// copy status flags
	p.fdstatus[newfd] = p.fdstatus[oldfd];

	// we return the USER fd, not the KERNEL fd which should be private to us
	return newfd;
}

int fcntl(int fildes, int cmd, ...)
{
	extern int (*stdin_fn_fcntl)(int, int, va_list);

	if (fildes < 0 || fildes >= Process::MAX_FDS)
	{
		errno = EBADF;
		return -1;
	}

	auto &p = get_current_process();
	const auto kernel_fd = p.fdtable[fildes];
	if (kernel_fd < 0)
	{
		errno = EBADF;
		return -1;
	}

	va_list ap;
	va_start(ap, cmd);

	if (kernel_fd == STDIN_FILENO && stdin_fn_fcntl)
		return stdin_fn_fcntl(fildes, cmd, ap);

	va_end(ap);

	errno = EINVAL;
	return -1;
}
}
