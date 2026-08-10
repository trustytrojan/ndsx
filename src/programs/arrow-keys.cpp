#include <cstdio>
#include <cstdlib>
#include <sys/unistd.h>

int main()
{
	puts("press q to exit");
	while (true)
	{
		// Just need 3 bytes for an arrow key ANSI escape sequence
		char buf[3]{};

		const auto bytes_read = read(STDIN_FILENO, buf, sizeof(buf));

		if (bytes_read == -1)
		{
			perror("read");
			return EXIT_FAILURE;
		}

		if (bytes_read != 3)
		{
			if (buf[0] == 'q')
				return EXIT_SUCCESS;
			printf("read() returned %d\n", bytes_read);
			continue;
		}

		if (buf[0] != '\e' || buf[1] != '[')
		{
			printf("received unknown byte sequence: %x %x %x\n", buf[0], buf[1], buf[2]);
			continue;
		}

		switch (buf[2])
		{
		case 'A':
			puts("up key pressed");
			break;
		case 'B':
			puts("down key pressed");
			break;
		case 'C':
			puts("right key pressed");
			break;
		case 'D':
			puts("left key pressed");
			break;
		}
	}
}
