#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
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
		constexpr auto bytes_to_write = sizeof(buf);
		const auto bytes_written = write(STDOUT_FILENO, buf, bytes_to_write);
		if (bytes_written == -1)
		{
			perror("my_prog: write");
			return EXIT_FAILURE;
		}
		printf("my_prog: wrote %d/%d bytes\n", bytes_written, bytes_to_write);
	}

	{ // read()
		char buf[6];
		puts("my_prog: calling read(), type in whatever you want");
		const auto bytes_read = read(STDIN_FILENO, buf, sizeof(buf) - 1);
		if (bytes_read == -1)
		{
			perror("my_prog: read");
			return EXIT_FAILURE;
		}
		buf[bytes_read] = '\0';
		printf("my_prog: read in %d/%d bytes: '%s'\n", bytes_read, sizeof(buf) - 1, buf);
	}

	{ // STDIN_FILENO reuse test
		if (close(STDIN_FILENO) == -1)
		{
			perror("my_prog: close");
			return EXIT_FAILURE;
		}

		const auto fd = open("test.txt", O_RDONLY);
		if (fd == -1)
		{
			perror("my_prog: open");
			return EXIT_FAILURE;
		}

		printf("my_prog: open returned %d\n", fd);
		if (fd != STDIN_FILENO)
		{
			puts("my_prog: expected STDIN_FILENO, failing");
			return EXIT_FAILURE;
		}

		char buf[100];
		const auto bytes_read = read(fd, buf, sizeof(buf) - 1);
		if (bytes_read == -1)
		{
			perror("my_prog: read");
			return EXIT_FAILURE;
		}

		buf[bytes_read] = '\0';
		printf("my_prog: read %d bytes from stdin: '%s'\n", bytes_read, buf);
	}

	{ // STDOUT_FILENO reuse test
		if (close(STDOUT_FILENO) == -1)
		{
			perror("my_prog: close");
			return EXIT_FAILURE;
		}

		/* picolibc's fclose() does NOT close() FDs of statically allocated FILEs! */
		/*if (fclose(stdout) == EOF)
		{
			perror("my_prog: fclose");
			return EXIT_FAILURE;
		}*/

		constexpr char buf1[] = "\e[31m";
		if (write(STDOUT_FILENO, buf1, sizeof(buf1)) != -1)
		{
			fputs("my_prog: expected write() to fail\n", stderr);
			return EXIT_FAILURE;
		}

		{ // fputs() + fflush()
			if (fputs(buf1, stdout) != EOF)
			{
				fputs("my_prog: expected fputs() to fail\n", stderr);
				return EXIT_FAILURE;
			}

			if (fflush(stdout) != EOF)
			{
				fputs("my_prog: expected fflush() to fail\n", stderr);
				return EXIT_FAILURE;
			}
		}

		// or you can add a newline to buf1 and remove the fflush().
		// same thing should happen, since stdout is line-buffered.

		// yes, nothing will be red for the duration of this program.
		// but once we return back to `init`, IT will have red output!
		// TODO: find a way to separate the buffers of programs... aka waste memory
		fputs("this text should NOT be red!\n", stderr);

		const auto fd = open("out-test.txt", O_WRONLY | O_CREAT);
		if (fd == -1)
		{
			perror("my_prog: open");
			return EXIT_FAILURE;
		}

		fprintf(stderr, "my_prog: open returned %d\n", fd);
		if (fd != STDOUT_FILENO)
		{
			fputs("my_prog: expected STDOUT_FILENO, failing", stderr);
			return EXIT_FAILURE;
		}

		constexpr char buf[] = "my_prog: write() test string\n";
		const auto bytes_written = write(fd, buf, sizeof(buf));
		if (bytes_written == -1)
		{
			perror("my_prog: write");
			return EXIT_FAILURE;
		}

		fprintf(stderr, "my_prog: wrote %d/%d bytes to stdout\n", bytes_written, sizeof(buf));
		fputs("my_prog: go view the file!\n", stderr);

		if (close(fd) == -1)
		{
			perror("my_prog: close");
			return EXIT_FAILURE;
		}
	}
}
