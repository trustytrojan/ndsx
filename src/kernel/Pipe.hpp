#pragma once

#include "CircularBuffer.hpp"
#include <cstdint>
#include <span>

struct Pipe
{
	static constexpr auto BUFFER_SIZE = 512;

	enum class End : bool
	{
		Read,
		Write
	};

	CircularBuffer<std::byte, BUFFER_SIZE> buf;
	uint8_t rw_ends{3}; // start with both ends "connected"

	bool end_connected(End end) const { return rw_ends & ((int)end + 1); }
	void disconnect_end(End end) { rw_ends &= ~((int)end + 1); }
	bool is_dead() const { return rw_ends == 0; }

	int read(std::span<std::byte> dst, bool nonblock);
	int write(std::span<const std::byte> src, bool nonblock);
};
