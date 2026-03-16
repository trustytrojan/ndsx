#include <cstdio>
#include <cstdlib>
#include <spawn.h>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <vector>

int main()
{
	puts("init program executed!");
	printf("init: getpid: %d\n", getpid());
	puts("init: spawning my_prog");

	std::vector<const char *> argv1{"hello", "my_prog 1", {}};

	pid_t pid;
	if (posix_spawn(&pid, "my_prog.dsl", {}, {}, (char **)argv1.data(), {}) == -1)
	{
		puts("init: posix_spawn failed");
		return EXIT_FAILURE;
	}

	// std::vector<const char *> argv2{"hello", "my_prog 2", {}};

	// pid_t pid2;
	// if (posix_spawn(&pid, "my_prog.dsl", {}, {}, (char **)argv2.data(), {}) == -1)
	// {
	// 	puts("init: posix_spawn failed");
	// 	return EXIT_FAILURE;
	// }

	puts("init: spawned, waiting for my_progs...");

	int status;
	if (const auto cpid = wait(&status))
	{
		printf("init: wait returned %d\n", cpid);

		if (!WIFEXITED(status))
		{
			puts("init: my_prog did not exit normally!");
			return EXIT_FAILURE;
		}

		printf("init: my_prog exited with code %d\n", WEXITSTATUS(status));
	}

	// for (int i = 0; i < 2; ++i)
	// {
	// 	if (const auto cpid = wait(&status))
	// 	{
	// 		printf("init: wait returned %d\n", cpid);

	// 		if (cpid == -1)
	// 		{
	// 			puts("init: no child processes left, breaking loop");
	// 			break;
	// 		}

	// 		if (!WIFEXITED(status))
	// 		{
	// 			puts("init: my_prog did not exit normally!");
	// 			return EXIT_FAILURE;
	// 		}

	// 		printf("init: my_prog exited with code %d\n", WEXITSTATUS(status));
	// 	}
	// }

	puts("init: returning");
}
