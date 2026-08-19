#include "Pipe.hpp"
#include "pipe_ops.hpp"
#include "process_manager.hpp"
#include <array>
#include <cerrno>
#include <memory>
#include <new>

namespace
{
using PipeIndex = int8_t;

uint8_t _in_use = 0;
constexpr auto MAX_COUNT = 8 * sizeof(_in_use);

std::array<std::unique_ptr<Pipe>, MAX_COUNT> pipes;

// --- FD Encoding Helpers ---
constexpr int make_kfd(PipeIndex idx, Pipe::End end) noexcept
{
	return _pipe::KFD_PREFIX | (idx << 1) | static_cast<uint8_t>(end);
}

constexpr PipeIndex index_from_kfd(int kfd) noexcept
{
	return static_cast<PipeIndex>((kfd >> 1) & 0x1F);
}

constexpr Pipe::End end_from_kfd(int kfd) noexcept
{
	return static_cast<Pipe::End>(kfd & 1);
}

// --- Slot Allocation ---
PipeIndex allocate()
{
	const auto free_bits = ~_in_use;
	if (free_bits == 0)
	{
		errno = ENFILE;
		return -1;
	}

	const auto idx = static_cast<PipeIndex>(__builtin_ctz(free_bits));

	auto ptr = std::unique_ptr<Pipe>(new (std::nothrow) Pipe);
	if (!ptr)
	{
		errno = ENOMEM;
		return -1;
	}

	pipes[idx] = std::move(ptr);
	_in_use |= (1U << idx);
	return idx;
}

void free_slot(PipeIndex idx)
{
	if (idx >= 0 && idx < static_cast<PipeIndex>(MAX_COUNT))
	{
		pipes[idx].reset();
		_in_use &= ~(1U << idx);
	}
}

Pipe *get(PipeIndex idx)
{
	if (idx < 0 || idx >= static_cast<PipeIndex>(MAX_COUNT))
		return nullptr;
	return pipes[idx].get();
}
} // anonymous namespace

// --- Public API Implementations ---
namespace _pipe
{

int read(int kfd, void *buf, std::size_t nbyte, bool nonblock)
{
	if (!is_kfd(kfd))
	{
		errno = EBADF;
		return -1;
	}

	const auto idx = index_from_kfd(kfd);
	const auto p = get(idx);
	if (!p || end_from_kfd(kfd) != Pipe::End::Read)
	{
		errno = EBADF;
		return -1;
	}

	return p->read({(std::byte *)buf, nbyte}, nonblock);
}

int write(int kfd, const void *buf, std::size_t nbyte, bool nonblock)
{
	if (!is_kfd(kfd))
	{
		errno = EBADF;
		return -1;
	}

	const auto idx = index_from_kfd(kfd);
	const auto p = get(idx);
	if (!p || end_from_kfd(kfd) != Pipe::End::Write)
	{
		errno = EBADF;
		return -1;
	}

	return p->write({(const std::byte *)buf, nbyte}, nonblock);
}

int close(int kfd)
{
	if (!is_kfd(kfd))
	{
		errno = EBADF;
		return -1;
	}

	const auto idx = index_from_kfd(kfd);
	const auto p = get(idx);
	if (!p)
	{
		errno = EBADF;
		return -1;
	}

	p->disconnect_end(end_from_kfd(kfd));
	if (p->is_dead())
		free_slot(idx);

	return 0;
}

} // namespace _pipe

// --- POSIX C Syscall Entry ---
extern "C" int pipe(int fildes[2])
{
	if (!fildes)
	{
		errno = EFAULT;
		return -1;
	}

	const auto idx = allocate();
	if (idx < 0)
	{
		// allocate() sets errno appropriately
		return -1;
	}

	// pipe is ready, now find slots in the calling process to put the kds in
	auto &p = get_current_process();

	const auto insert_kfd_into_process = [&](Pipe::End end)
	{
		auto slot = p.find_first_open_fd_slot();
		if (slot >= Process::MAX_FDS)
		{
			errno = ENFILE;
			return -1;
		}
		p.fdtable[slot] = make_kfd(idx, end);
		fildes[(bool)end] = slot;
		return 0;
	};

	if (insert_kfd_into_process(Pipe::End::Read) == -1)
		return -1;
	if (insert_kfd_into_process(Pipe::End::Write) == -1)
		// Might want to undo the insertion of the read fd?
		return -1;

	return 0;
}
