#pragma once

#include <cstddef>

namespace _pipe
{
// Core descriptor check
constexpr auto KFD_PREFIX = 0x40000000;

constexpr bool is_kfd(int kfd) noexcept
{
	return (kfd & 0x7FFFFFFF) >= KFD_PREFIX;
}

// Public-facing syscall implementations called by fd.cpp
int read(int kfd, void *buf, std::size_t count, bool nonblock);
int write(int kfd, const void *buf, std::size_t count, bool nonblock);
int close(int kfd);

} // namespace _pipe
