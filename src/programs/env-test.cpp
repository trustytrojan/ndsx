#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace std::literals;

int main(int, char **, char **envp)
{
	puts("env-test:");

	puts(" initial envp:");
	for (auto s{envp}; *s; ++s)
		printf("  '%s'\n", *s);

	// printf(" getenv test:\n  X='%s' Y='%s' Z='%s'\n", getenv("X"), getenv("Y"), getenv("Z"));

	// puts(" setenv test: Y <- 67");
	setenv("Y", "67", 1);
	// const auto yval = getenv("Y");
	// printf("  Y='%s'\n", yval);
	// if (yval != "67"sv)
	// 	puts("  FAILED");

	// puts(" unsetenv test: Z");
	unsetenv("Z");
	// const auto zval = getenv("Z");
	// printf("  Z='%s'\n", zval);
	// if (zval)
	// 	puts("  FAILED");

	puts(" final envp:");
	for (auto s{envp}; *s; ++s)
		printf("  '%s'\n", *s);
}
