#pragma once

#include <array>

template <typename T, std::size_t Capacity>
class CircularBuffer
{
public:
	using value_type = T;
	using size_type = std::size_t;

	// Capacity & Size
	[[nodiscard]] constexpr size_type capacity() const noexcept { return Capacity; }
	[[nodiscard]] constexpr size_type size() const noexcept { return count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
	[[nodiscard]] constexpr bool full() const noexcept { return count == Capacity; }

	// Clear
	constexpr void clear() noexcept
	{
		head = 0;
		tail = 0;
		count = 0;
	}

	// Push back (returns false if full)
	constexpr bool push_back(const T &item) noexcept
	{
		if (full())
			return false;
		data[tail] = item;
		tail = (tail + 1) % Capacity;
		++count;
		return true;
	}

	// Pop front (returns false if empty)
	constexpr bool pop_front() noexcept
	{
		if (empty())
			return false;
		head = (head + 1) % Capacity;
		--count;
		return true;
	}

	// Element access
	[[nodiscard]] constexpr T &front() noexcept { return data[head]; }
	[[nodiscard]] constexpr const T &front() const noexcept { return data[head]; }

	[[nodiscard]] constexpr T &back() noexcept
	{
		size_type idx = (tail == 0) ? (Capacity - 1) : (tail - 1);
		return data[idx];
	}

	[[nodiscard]] constexpr const T &back() const noexcept
	{
		size_type idx = (tail == 0) ? (Capacity - 1) : (tail - 1);
		return data[idx];
	}

	// Vector-like index access
	[[nodiscard]] constexpr T &operator[](size_type index) noexcept { return data[(head + index) % Capacity]; }

	[[nodiscard]] constexpr const T &operator[](size_type index) const noexcept
	{
		return data[(head + index) % Capacity];
	}

private:
	std::array<T, Capacity> data{};
	size_type head{};
	size_type tail{};
	size_type count{};
};
