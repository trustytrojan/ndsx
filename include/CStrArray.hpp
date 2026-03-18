#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

struct CStrArray
{
	size_t count{};
	char **data{};

	constexpr CStrArray() = default;

	// Deep copy from an existing null-terminated array
	// If any allocation fails, `data` will be NULL.
	constexpr explicit CStrArray(char *const *src)
	{
		if (!src)
			return;

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
				fputs("kernel: CStrArray: strdup failed, freeing everything\n", stderr);
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

	// Clean up the entire structure
	constexpr void clear()
	{
		if (!data)
			return;
		for (size_t i = 0; i < count; ++i)
			if (data[i])
				free(data[i]);
		free(data);
		data = {};
		count = 0;
	}

	constexpr ~CStrArray() { clear(); }

	// Manual Move Semantics (The C++ "Nice Semantics" part)
	constexpr CStrArray(CStrArray &&other) noexcept
		: data(std::exchange(other.data, nullptr)),
		  count(std::exchange(other.count, 0))
	{
	}

	constexpr CStrArray &operator=(CStrArray &&other) noexcept
	{
		if (this != &other)
		{
			clear();
			data = std::exchange(other.data, nullptr);
			count = std::exchange(other.count, 0);
		}
		return *this;
	}

	// Explicitly forbid copying to prevent accidental double-frees
	constexpr CStrArray(const CStrArray &) = delete;
	constexpr CStrArray &operator=(const CStrArray &) = delete;
};
