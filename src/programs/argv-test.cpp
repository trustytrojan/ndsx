#include <cstdio>
#include <sys/unistd.h>

int main(int argc, char **argv)
{
	printf("argv-test: argc: %d\n", argc);
	printf("argv-test: printing args:\n");
	for (int i = 0; i < argc; ++i)
		printf("  %d: '%s'\n", i, argv[i]);
}
