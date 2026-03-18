#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

struct CStrArray
{
	char **data{};

	constexpr CStrArray() = default;

	// Deep copy from an existing null-terminated array
	// If any allocation fails, `data` will be NULL.
	constexpr explicit CStrArray(char *const *src)
	{
		if (!src)
			return;

		size_t count{};
		while (src[count])
			++count;
		// printf("kernel: CStrArray: count: %d\n", count);

		if (!count)
			return;

		data = (char **)calloc(count + 1, sizeof(char *));
		if (!data)
			return;

		// printf("kernel: CStrArray: calloc'd data: %p\n", data);

		for (size_t i = 0; i < count; ++i)
			if (src[i] && !(data[i] = strdup(src[i])))
			{
				fputs("kernel: CStrArray: strdup failed\n", stderr);
				// Memory error! Free everything.
				for (size_t j = 0; j < i; ++j)
					free(data[j]);
				free(data);
				data = {};
				count = 0;
				return;
			}
		// printf("kernel: CStrArray: returning: data=%p count=%d\n", data, count);
	}

	// Count how many strings are in the array.
	constexpr size_t count()
	{
		if (!data)
			return 0;
		size_t count{};
		while (data[count])
			++count;
		return count;
	}

	// Frees all strings and the array itself.
	constexpr void clear()
	{
		if (!data)
			return;
		for (auto s{data}; *s; ++s)
			free(*s);
		free(data);
		data = {};
	}

	constexpr ~CStrArray() { clear(); }

	// Move constructor
	constexpr CStrArray(CStrArray &&other) noexcept
	{
		clear();
		data = std::exchange(other.data, nullptr);
	}

	// Move assignment
	constexpr CStrArray &operator=(CStrArray &&other) noexcept
	{
		if (this != &other)
		{
			clear();
			data = std::exchange(other.data, nullptr);
		}
		return *this;
	}

	// Explicitly forbid copying to prevent accidental double-frees
	constexpr CStrArray(const CStrArray &) = delete;
	constexpr CStrArray &operator=(const CStrArray &) = delete;
};
