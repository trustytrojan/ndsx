#include "DylibManager.hpp"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
// #include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

using namespace std::literals;

#define DSL_MAGIC 0x304C5344

static auto read_dep_names(FILE *const f) -> std::expected<std::vector<std::string>, std::string>
{
	int dep_count;

	if (fread(&dep_count, sizeof(int), 1, f) != 1)
		return std::unexpected{"fread: "s + strerror(errno)};

	if (dep_count == DSL_MAGIC)
	{
		// this DSL wasn't made with the auto-deps dsltool. seek backwards.
		if (fseek(f, 0, SEEK_SET) == -1)
			return std::unexpected{"fseek: "s + strerror(errno)};
		return {};
	}

	if (dep_count == 0)
		return {};

	std::vector<std::string> dep_names;

	for (int i{}; i < dep_count; ++i)
	{
		std::string name;

		// The strings are null-terminated, so this will break when a null byte is hit.
		while (const auto c{fgetc(f)})
		{
			if (c == EOF)
			{
				if (feof(f))
					return std::unexpected{"end of file reached"};
				if (ferror(f))
					return std::unexpected{"fgetc: "s + strerror(errno)};
			}
			name += c;
		}

		dep_names.emplace_back(std::move(name));
	}

	return dep_names;
}

std::span<void *const> deps;

bool dep_symbol_resolver(const char *const name, uint32_t *const value, const uint32_t attributes)
{
	// Prevent DSLs from accessing our renamed libnds functions!
	if ((attributes & DSL_SYMBOL_MAIN_BINARY) && strstr(name, "libnds_") == name) // equivalent of `starts_with()`
	{
		fprintf(stderr, "kernel: blocked access to symbol '%s'\n", name);
		return false;
	}

	// Only resolve symbols marked unresolved by dsltool.
	// This means already-resolved main binary symbols won't pass this.
	if (!(attributes & DSL_SYMBOL_UNRESOLVED))
		return true;

	// Search all dependencies for the symbol's name.
	for (const auto dep : deps)
		if ((*value = (uint32_t)dlsym(dep, name)))
			// Symbol resolved!
			return true;

	// Failed to resolve the symbol. Return false to stop relocation.
	return false;
}

// this mitigates the -Wignored-attributes caused by decltype(&fclose)
struct FileCloser
{
	auto operator()(FILE *f) const noexcept -> void
	{
		if (f)
			(void)fclose(f);
	}
};

namespace DylibManager
{

static std::vector<std::string> loading_libs;
static constexpr auto lib_is_loading(std::string_view lib)
{
	return std::ranges::find(loading_libs, lib) != loading_libs.end();
}

struct LoadedLib
{
	std::string name;
	void *handle;
	int refs;
	std::vector<Handle> deps;
};
static std::vector<LoadedLib> loaded_libs;
static constexpr auto find_loaded_lib(std::string_view name)
{
	return std::ranges::find_if(loaded_libs, [&](auto &lib) { return lib.name == name; });
}
static constexpr auto find_loaded_lib(void *handle)
{
	return std::ranges::find_if(loaded_libs, [=](auto &lib) { return lib.handle == handle; });
}

auto unref_lib(void *handle) -> void
{
	if (!handle)
		return;
	const auto lib{find_loaded_lib(handle)};
	if (lib == loaded_libs.end() || lib->refs <= 0)
		return;
	--lib->refs;
	// std::cerr << "unref'd lib '" << lib->name << "': refs=" << lib->refs << '\n';
}

auto open_lib(const std::string &path) -> std::expected<Handle, std::string>
{
	if (!path.ends_with(".dsl"))
		return std::unexpected{"must be a .dsl file!"};

	const auto lib_name{path.substr(path.find_last_of('/') + 1, path.length() - 4)};

	if (const auto it{find_loaded_lib(lib_name)}; it != loaded_libs.end())
	{
		++it->refs;
		// std::cerr << "lib '" << lib_name << "' already loaded: refs=" << it->refs << '\n';
		return it->handle;
	}

	std::unique_ptr<FILE, FileCloser> file{fopen(path.c_str(), "r")};
	if (!file)
		return std::unexpected{"fopen: "s + strerror(errno)};

	const auto dep_names{read_dep_names(file.get())};
	if (!dep_names)
		return std::unexpected{"read_dep_names: " + dep_names.error()};

	loading_libs.emplace_back(lib_name);

	// this is the killer. since Handle objects are `unique_ptr`s,
	// if we need to error-return for any reason, all deps in here
	// will properly unref themselves!
	std::vector<Handle> dep_handles;

	for (const auto &dep_name : *dep_names)
	{
		if (lib_is_loading(dep_name))
		{
			loading_libs.pop_back();
			return std::unexpected{"circular dependency with '" + dep_name + "'"};
		}

		auto dep_lib{open_lib(dep_name + ".dsl")};
		if (!dep_lib)
		{
			loading_libs.pop_back();
			return std::unexpected{"dep '" + dep_name + "': " + dep_lib.error()};
		}

		dep_handles.emplace_back(std::move(*dep_lib));
	}

	loading_libs.pop_back();

	// map handles to their pointers
	const auto dep_ptrs{dep_handles | std::views::transform(&Handle::get) | std::ranges::to<std::vector>()};

	// give the deps to our symbol resolver
	deps = dep_ptrs;

	// actual library opening
	const auto handle{dlopen_FILE(file.get(), RTLD_NOW | RTLD_LOCAL)};
	if (!handle)
		return std::unexpected{"dlopen_FILE: "s + dlerror()};

	// store a new LoadedLib, moving all our dependency `Handle`s into it!
	const auto &lib = loaded_libs.emplace_back(lib_name, handle, 1, std::move(dep_handles));

	// std::cerr << "opened lib '" << lib_name << "': refs=" << lib.refs << '\n';

	return handle; // converted to `Handle`
}

// only meant to be used by gc()
static auto erase_lib(const std::vector<LoadedLib>::iterator lib)
{
	// std::cerr << "collecting lib '" << lib->name << "'\n";
	dlclose(lib->handle);
	for (const auto &dep : lib->deps)
		unref_lib(dep.get());
	// continue iteration of gc() loop
	return loaded_libs.erase(lib);
}

auto gc() -> void
{
	bool erased_any;
	do
	{
		erased_any = false;
		for (auto it{loaded_libs.begin()}; it != loaded_libs.end();)
		{
			if (it->refs > 0)
			{
				++it;
				continue;
			}

			it = erase_lib(it);
			erased_any = true;
		}
	} while (erased_any);
}

} // namespace DylibManager
