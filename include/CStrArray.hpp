#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
// #include <string>
#include <utility>

struct CStrArray
{
	char **data{};
	// std::string name;

	constexpr CStrArray() = default;

	// Deep copy from an existing null-terminated array
	// If any allocation fails, `data` will be NULL.
	constexpr explicit CStrArray(char *const *src /*, const std::string &name*/)
	// : name{name}
	{
		if (!src)
			return;

		size_t count{};
		while (src[count])
			++count;
		// printf("CStrArray(%p): count: %d\n", this, count);

		if (!count)
			return;

		data = (char **)calloc(count + 1, sizeof(char *));
		if (!data)
			return;

		// printf("[c] t=%p\n", this);

		for (size_t i = 0; i < count; ++i)
			if (src[i] && !(data[i] = strdup(src[i])))
			{
				puts("CStrArray: strdup failed");
				// Memory error! Free everything.
				for (size_t j = 0; j < i; ++j)
					free(data[j]);
				free(data);
				data = {};
				count = 0;
				return;
			}
		// printf("C(%s): t=%p d=%p\n", name.c_str(), this, data);
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
		// printf("c(%s): t=%p d=%p\n", name.c_str(), this, data);
		for (auto s{data}; *s; ++s)
		{
			// printf("CStrArray: freeing %p '%s'\n", *s, *s);
			free(*s);
		}
		// printf("CStrArray: freeing %p\n", data);
		free(data);
		data = {};
	}

	constexpr ~CStrArray() { clear(); }

	// Move constructor
	constexpr CStrArray(CStrArray &&other) noexcept
	{
		clear();
		data = std::exchange(other.data, nullptr);
		// printf("m(%s): t=%p o=%p d=%p\n", name.c_str(), this, &other, data);
	}

	// Move assignment
	constexpr CStrArray &operator=(CStrArray &&other) noexcept
	{
		if (this != &other)
		{
			clear();
			data = std::exchange(other.data, nullptr);
			// printf("m(%s): t=%p o=%p d=%p\n", name.c_str(), this, &other, data);
		}
		return *this;
	}

	constexpr CStrArray(const CStrArray &other)
		: CStrArray(other.data /*, other.name*/)
	{
	}

	constexpr CStrArray &operator=(const CStrArray &) = delete;
};
