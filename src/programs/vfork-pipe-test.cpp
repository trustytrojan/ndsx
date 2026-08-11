#include <sys/unistd.h>

#include <cstdio>
#include <cstdlib>

// String literals include a null-terminator, so CHILD_MSG_SIZE will include it.
#define CHILD_MSG "hello from child"
#define CHILD_MSG_SIZE sizeof(CHILD_MSG)

int main()
{
	// clang-format off
	struct { int r, w; } p;
	// clang-format on

	if (pipe((int *)&p) == -1)
	{
		perror("parent: pipe");
		return EXIT_FAILURE;
	}

	printf("parent: p={r=%d w=%d}\n", p.r, p.w);

	switch (vfork())
	{
	case -1:
		perror("vfork");
		return EXIT_FAILURE;
	case 0:
		// we share all memory, even this thread, with the parent.
		// do what we need to do and exit.
		if (close(p.r) == -1)
			perror("child: close(p.r)");

		const auto bytes_written = write(p.w, CHILD_MSG, CHILD_MSG_SIZE);
		if (bytes_written == -1)
		{
			perror("child: write");
			if (close(p.w) == -1)
				perror("child: close(p.w)");
			puts("child: _exit(1)");
			_exit(1);
		}
		printf("child: wrote %d bytes to pipe: '%s'\n", bytes_written, CHILD_MSG);

		if (close(p.w) == -1)
			perror("child: close(p.w)");

		puts("child: _exit(0)");
		_exit(0);
	}

	// the child has exited at this point, so we can safely return from main().
	char buf[CHILD_MSG_SIZE]{};
	const auto bytes_read = read(p.r, buf, CHILD_MSG_SIZE);
	if (bytes_read == -1)
	{
		perror("parent: read");
		return EXIT_FAILURE;
	}

	printf("parent: read %d bytes from pipe: '%s'\n", bytes_read, buf);

	if (close(p.r) == -1)
		perror("close");

	if (close(p.w) == -1)
		perror("close");
}
