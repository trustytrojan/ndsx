#include <fnmatch.h>
#include <glob.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{

void glob_recursive(const fs::path &current_dir, std::string_view pattern, std::vector<std::string> &results, int flags)
{
	auto slash_pos = pattern.find('/');

	// Determine fnmatch flags based on glob requirements
	int fnmatch_flags = FNM_PATHNAME | FNM_PERIOD;

	// Base Case: Filename-level pattern matching
	if (slash_pos == std::string_view::npos)
	{
		std::error_code ec;
		fs::directory_iterator it(current_dir.empty() ? "." : current_dir, ec);
		if (ec)
			return;

		std::string pattern_str(pattern);

		for (const auto &entry : it)
		{
			std::string filename = entry.path().filename().string();

			// Match using standard libc fnmatch
			if (fnmatch(pattern_str.c_str(), filename.c_str(), fnmatch_flags) == 0)
			{
				fs::path full_path =
					current_dir.empty() ? entry.path().filename() : current_dir / entry.path().filename();
				results.push_back(full_path.string());
			}
		}
		return;
	}

	// Recursive Case: Directory segment expansion
	std::string_view segment = pattern.substr(0, slash_pos);
	std::string_view remaining = pattern.substr(slash_pos + 1);

	// If current segment contains no wildcards, navigate directly
	if (!segment.contains('*') && !segment.contains('?') && !segment.contains('['))
	{
		fs::path next_dir = current_dir.empty() ? fs::path(segment) : current_dir / segment;
		glob_recursive(next_dir, remaining, results, flags);
		return;
	}

	// Expand wildcard directory segment
	std::error_code ec;
	fs::directory_iterator it(current_dir.empty() ? "." : current_dir, ec);
	if (ec)
		return;

	std::string segment_str(segment);

	for (const auto &entry : it)
	{
		std::string filename = entry.path().filename().string();

		if (fnmatch(segment_str.c_str(), filename.c_str(), fnmatch_flags) == 0)
		{
			if (entry.is_directory(ec))
			{
				fs::path next_dir =
					current_dir.empty() ? entry.path().filename() : current_dir / entry.path().filename();
				glob_recursive(next_dir, remaining, results, flags);
			}
		}
	}
}

} // anonymous namespace

extern "C"
{
int glob(const char *pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *pglob)
{
	if (!pattern || !pglob)
		return GLOB_NOSPACE;

	std::vector<std::string> matches;
	std::string_view pat(pattern);

	fs::path initial_dir;
	if (pat.starts_with('/'))
	{
		initial_dir = "/";
		pat.remove_prefix(1);
	}

	glob_recursive(initial_dir, pat, matches, flags);

	// Handle GLOB_NOCHECK flag
	if (matches.empty())
	{
		if (flags & GLOB_NOCHECK)
			matches.push_back(pattern);
		else
			return GLOB_NOMATCH;
	}

	// Sort results unless explicitly disabled
	if (!(flags & GLOB_NOSORT))
		std::ranges::sort(matches);

	// Populate POSIX glob_t C structure
	pglob->gl_pathc = matches.size();
	pglob->gl_pathv = static_cast<char **>(std::malloc((matches.size() + 1) * sizeof(char *)));
	if (!pglob->gl_pathv)
		return GLOB_NOSPACE;

	for (size_t i = 0; i < matches.size(); ++i)
		pglob->gl_pathv[i] = strdup(matches[i].c_str());
	pglob->gl_pathv[matches.size()] = nullptr;

	return 0;
}

void globfree(glob_t *pglob)
{
	if (!pglob || !pglob->gl_pathv)
		return;

	for (size_t i = 0; i < pglob->gl_pathc; ++i)
		std::free(pglob->gl_pathv[i]);
	std::free(pglob->gl_pathv);
	pglob->gl_pathv = nullptr;
	pglob->gl_pathc = 0;
}

} // extern "C"
