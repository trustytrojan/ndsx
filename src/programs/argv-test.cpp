#include <cstdio>
#include <sys/unistd.h>

int main(int argc, char **argv)
{
	printf("argc: %d\n", argc);

	if (!argv)
	{
		printf("argv is NULL\n");
		return 0;
	}

	for (int i = 0; i < argc; ++i)
		printf("%d: '%s'\n", i, argv[i]);
}
