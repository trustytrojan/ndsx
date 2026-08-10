#include "Pipe.hpp"
#include <cerrno>
#include <nds/cothread.h>

// https://www.man7.org/linux/man-pages/man3/read.3p.html#:~:text=When%20attempting%20to%20read%20from%20an%20empty%20pipe%20or%20FIFO%3A
int Pipe::read(std::span<std::byte> dst, bool nonblock)
{
	size_t bytes_read = 0;

	while (bytes_read < dst.size())
	{
		if (buf.empty())
		{
			if (!end_connected(End::Write))
				break;

			if (nonblock)
			{
				if (bytes_read == 0)
				{
					errno = EAGAIN;
					return -1;
				}
				break;
			}

			// Dangerous design decision, considering how much cothread_yield() does in ndsx.
			// However, this would prevent the CPU from spinning here indefinitely.
			cothread_yield();

			continue;
		}

		dst[bytes_read++] = buf.front();
		buf.pop_front();
	}

	return bytes_read;
}

// https://www.man7.org/linux/man-pages/man3/write.3p.html#:~:text=Write%20requests%20to%20a%20pipe%20or%20FIFO
int Pipe::write(std::span<const std::byte> src, bool nonblock)
{
	size_t bytes_written = 0;

	while (bytes_written < src.size())
	{
		if (buf.full())
		{
			if (!end_connected(End::Read))
			{
				errno = EPIPE;
				// TODO: send SIGPIPE to the calling thread
				return -1;
			}

			if (nonblock)
			{
				if (bytes_written == 0)
				{
					errno = EAGAIN;
					return -1;
				}
				break;
			}

			// Dangerous design decision, considering how much cothread_yield() does in ndsx.
			// However, this would prevent the CPU from spinning here indefinitely.
			cothread_yield();

			continue;
		}

		buf.push_back(src[bytes_written++]);
	}

	return bytes_written;
}
