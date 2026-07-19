#include <process_manager.hpp>

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <sys/_default_fcntl.h>

// File descriptor system calls.
#include <array>
#include <cstring>

// File descriptor system calls.

int ndsx_keyboardGetChar()
{
	static int shown = 0;
	int c = -1;

	if (shown == 0)
	{
		keyboardShow();
		shown = 1;
	}

	while (true)
	{
		scanKeys();
		c = keyboardUpdate();
		if (c > 0)
			break;
		// cothread_yield_irq(IRQ_VBLANK);
		cothread_yield();
	}

	if (c == '\n')
	{
		keyboardHide();
		shown = 0;
	}

	return c;
}

extern "C"
{
// Pipe implementation
static constexpr int PIPE_BUF_SIZE = 512; // per-user request
static constexpr int MAX_PIPES = 32;
static constexpr int PIPE_KFD_BASE = -1000; // kernel fd encoding base

struct Pipe
{
	std::array<char, PIPE_BUF_SIZE> buf;
	size_t head{0};
	size_t tail{0};
	size_t count{0};
	int readers{0};
	int writers{0};
	uid_t uid{0};
	gid_t gid{0};
};

static std::array<Pipe, MAX_PIPES> pipes;
static std::array<bool, MAX_PIPES> pipe_in_use{};

static int allocate_pipe()
{
	for (int i = 0; i < MAX_PIPES; ++i)
	{
		if (!pipe_in_use[i])
		{
			pipe_in_use[i] = true;
			pipes[i] = Pipe{};
			return i;
		}
	}
	return -1;
}

static void free_pipe(int idx)
{
	if (idx >= 0 && idx < MAX_PIPES)
		pipe_in_use[idx] = false;
}

static int kfd_for(int pipe_idx, int end)
{
	// end: 0 = read, 1 = write
	return PIPE_KFD_BASE - (pipe_idx * 2 + end);
}

static bool is_pipe_kfd(int kfd)
{
	return kfd <= PIPE_KFD_BASE && kfd > PIPE_KFD_BASE - MAX_PIPES * 2 - 10;
}

static int pipe_index_from_kfd(int kfd)
{
	const int v = PIPE_KFD_BASE - kfd;
	return v / 2;
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

	// pipe kernel fds
	if (is_pipe_kfd(kernel_fd))
	{
		const int idx = pipe_index_from_kfd(kernel_fd);
		const int end = (PIPE_KFD_BASE - kernel_fd) % 2; // 0 read, 1 write
		if (idx >= 0 && idx < MAX_PIPES && pipe_in_use[idx])
		{
			if (end == 0)
				--pipes[idx].readers;
			else
				--pipes[idx].writers;

			// if both ends closed, free
			if (pipes[idx].readers <= 0 && pipes[idx].writers <= 0)
				free_pipe(idx);
		}

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
			const char c = ndsx_keyboardGetChar();
			if (c <= 0)
				// keyboard uninitialized, or other problem
				break;
			*cp = c;

			// echo the characters for now. later on we can implement the termios API.
			extern ConsoleOutFn libnds_stdout_write, libnds_stderr_write;
			libnds_stdout_write(&c, 1);
		}
		return cp - (char *)ptr;
	}

	// pipe read
	if (is_pipe_kfd(kernel_fd))
	{
		const int idx = pipe_index_from_kfd(kernel_fd);
		const int end = (PIPE_KFD_BASE - kernel_fd) % 2; // 0 read, 1 write
		if (idx < 0 || idx >= MAX_PIPES || !pipe_in_use[idx])
		{
			errno = EBADF;
			return -1;
		}

		if (end != 0)
		{
			errno = EBADF;
			return -1;
		}

		auto &pipe = pipes[idx];
		// Non-blocking?
		const bool nonblock = (p.fdstatus[fd] & O_NONBLOCK) != 0;

		size_t toread = len;
		size_t got = 0;
		while (toread > 0)
		{
			if (pipe.count == 0)
			{
				if (pipe.writers == 0)
				{
					// EOF
					break;
				}
				if (nonblock)
				{
					if (got == 0)
					{
						errno = EAGAIN;
						return -1;
					}
					break;
				}
				// wait for writer to produce data
				cothread_yield();
				continue;
			}

			// read one byte at a time (small buffers)
			((char *)ptr)[got++] = pipe.buf[pipe.head];
			pipe.head = (pipe.head + 1) % PIPE_BUF_SIZE;
			--pipe.count;
			--toread;
		}

		return got;
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
	// pipe write
	if (is_pipe_kfd(kernel_fd))
	{
		const int idx = pipe_index_from_kfd(kernel_fd);
		const int end = (PIPE_KFD_BASE - kernel_fd) % 2; // 0 read, 1 write
		if (idx < 0 || idx >= MAX_PIPES || !pipe_in_use[idx])
		{
			errno = EBADF;
			return -1;
		}
		if (end != 1)
		{
			errno = EBADF;
			return -1;
		}

		auto &pipe = pipes[idx];
		const bool nonblock = (p.fdstatus[fd] & O_NONBLOCK) != 0;

		size_t towrite = len;
		size_t written = 0;
		while (towrite > 0)
		{
			if (pipe.count >= PIPE_BUF_SIZE)
			{
				if (pipe.readers == 0)
				{
					errno = EPIPE;
					return -1;
				}
				if (nonblock)
				{
					if (written == 0)
					{
						errno = EAGAIN;
						return -1;
					}
					break;
				}
				// wait for reader to consume
				cothread_yield();
				continue;
			}

			pipe.buf[pipe.tail] = ((const char *)ptr)[written++];
			pipe.tail = (pipe.tail + 1) % PIPE_BUF_SIZE;
			++pipe.count;
			--towrite;
		}

		return written;
	}

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

int pipe(int fildes[2])
{
	if (!fildes)
	{
		errno = EFAULT;
		return -1;
	}

	auto &p = get_current_process();

	// find two free user fds
	int r = 0;
	for (; r < Process::MAX_FDS && p.fdtable[r] >= 0; ++r)
		;
	int w = r + 1;
	for (; w < Process::MAX_FDS && p.fdtable[w] >= 0; ++w)
		;

	if (r >= Process::MAX_FDS || w >= Process::MAX_FDS)
	{
		errno = EMFILE;
		return -1;
	}

	const int idx = allocate_pipe();
	if (idx < 0)
	{
		errno = ENFILE;
		return -1;
	}

	pipes[idx].readers = 1;
	pipes[idx].writers = 1;
	pipes[idx].uid = 0;
	pipes[idx].gid = 0;

	const int kfd_r = kfd_for(idx, 0);
	const int kfd_w = kfd_for(idx, 1);

	p.fdtable[r] = kfd_r;
	p.fdtable[w] = kfd_w;
	p.fdflags[r] = 0;
	p.fdflags[w] = 0;
	p.fdstatus[r] = 0;
	p.fdstatus[w] = 0;

	fildes[0] = r;
	fildes[1] = w;
	return 0;
}
}
