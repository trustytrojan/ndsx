#include <cstdio>
#include <cstdlib>
#include <sys/unistd.h>

int main(int argc, char **argv)
{
	puts("my_prog executed!");
	printf("my_prog: getpid: %d\n", getpid());
	printf("my_prog: getppid: %d\n", getppid());
	printf("my_prog: printing args:\n");
	for (int i = 0; i < argc; ++i)
		printf("  %d: '%s'\n", i, argv[i]);

	{ // write()
		constexpr char buf[] = "my_prog: write() test string\n";
		if (write(1, buf, sizeof(buf)) == -1)
		{
			perror("my_prog: write");
			return EXIT_FAILURE;
		}
	}

	{ // read()
		char buf[6];
		if (read(STDIN_FILENO, buf, sizeof(buf) - 1) == -1)
		{
			perror("my_prog: read");
			return EXIT_FAILURE;
		}
		buf[sizeof(buf) - 1] = '\0';
		printf("my_prog: read in '%s'\n", buf);
	}
}
