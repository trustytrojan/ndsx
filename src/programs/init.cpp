#include <cstdio>
#include <cstdlib>
#include <span>
#include <spawn.h>
#include <string>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <vector>

int execute_process(const std::string &file, std::span<const char *> argv, std::span<const char *> envp)
{
	pid_t pid;
	if (posix_spawn(&pid, file.c_str(), {}, {}, (char **)argv.data(), (char **)envp.data()) == -1)
	{
		perror("init: posix_spawn");
		return EXIT_FAILURE;
	}
	printf("init: child %d spawned\n", pid);

	int status;
	const auto rpid = waitpid(pid, &status, 0);
	if (rpid == -1)
	{
		perror("init: waitpid");
		return EXIT_FAILURE;
	}

	if (rpid != pid)
	{
		fputs("init: waitpid() did not return child pid\n", stderr);
		return EXIT_FAILURE;
	}

	if (!WIFEXITED(status))
	{
		fputs("init: child did not exit normally\n", stderr);
		return EXIT_FAILURE;
	}

	printf("init: child %d exited with %d\n", pid, WEXITSTATUS(status));
	return EXIT_SUCCESS;
}

int argv_test(std::span<const char *> argv)
{
	return execute_process("argv-test.dsl", argv, {});
}

int env_test(std::span<const char *> envp)
{
	return execute_process("env-test.dsl", {}, envp);
}

int main()
{
	puts("init program executed!");
	printf("init: getpid: %d\n", getpid());

	{
		std::vector<const char *> argv{"argv-test.dsl", "arg0", "arg1", "arg2", {}};
		argv_test(argv);
	}

	{
		std::vector<const char *> envp{"X=1", "Y=2", "Z=3", {}};
		env_test(envp);
	}

	{
		std::vector<const char *> envp{"X=4", "Y=5", "Z=6", {}};
		env_test(envp);
	}

	puts("init: returning");
}
