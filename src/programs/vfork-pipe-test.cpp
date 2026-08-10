#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/unistd.h>

#define CHILD_MSG "hello from child\n"
#define CHILD_MSG_SIZE sizeof(CHILD_MSG)

int main()
{
	// clang-format off
	struct { int r, w; } p;
	// clang-format on

	if (pipe((int *)&p) == -1)
	{
		perror("pipe");
		return EXIT_FAILURE;
	}

	const volatile auto pid = vfork();
	printf("%d: vfork returned %d\n", getpid(), pid);

	switch (pid)
	{
	case -1:
		perror("vfork");
		return EXIT_FAILURE;
	case 0: // child
		// puts("child case");
		// we share all memory, even this thread, with the parent.
		// do what we need to do and exit.
		if (write(p.w, CHILD_MSG, CHILD_MSG_SIZE) == -1)
		{
			// puts("child: write returned -1, error will be printed, calling _exit(1)");
			// perror("child: write");
			// _exit(1);
			break;
		}
		else
		{
			// puts("child: write was successful, calling _exit(0)");
			// _exit(0);
			break;
		}
	default:
		puts("parent case");
		break;
	}

	printf("vfork returned %d\n", pid);
	printf("getpid: %d\n", getpid());

	printf("p.r: %d\n", p.r);
	printf("p.w: %d\n", p.w);

	if (pid == 0)
	{
		puts("pid is 0, exiting");
		_exit(0);
	}

	// the child has exited at this point, so we can safely return from main().
	char buf[CHILD_MSG_SIZE];
	if (read(p.r, buf, CHILD_MSG_SIZE) == -1)
	{
		perror("parent: read");
		return EXIT_FAILURE;
	}
	buf[CHILD_MSG_SIZE - 1] = '\0';

	printf("child says: '%s'\n", buf);

	if (close(p.r) == -1)
	{
		perror("close");
		return EXIT_FAILURE;
	}
	printf("closed p.r=%d\n", p.r);
	if (close(p.w) == -1)
	{
		perror("close");
		return EXIT_FAILURE;
	}
	printf("closed p.w=%d\n", p.w);
}
