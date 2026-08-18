#include <fcntl.h>
#include <unistd.h>

#include <nds.h>

int main()
{
	keyboardSetCanonicalMode(true);

	puts("calling read() with canonical mode on");

	// Canonical mode guarantees a newline at the end of the buffer,
	// regardless of the requested size. This means read() will block
	// until the user presses Enter. Any keypresses that would overflow
	// (sizeof(buf) - 1) are discarded to make room for the ending newline.
	char buf[3];
	int bytes_read = read(STDIN_FILENO, buf, sizeof(buf));

	if (bytes_read == -1)
	{
		perror("read");
		return EXIT_FAILURE;
	}

	if (bytes_read != 3)
	{
		fprintf(stderr, "bytes_read (%d) != %d\n", bytes_read, sizeof(buf));
		return EXIT_FAILURE;
	}

	printf("buf: '%.*s'\n", sizeof(buf), buf);

	keyboardSetCanonicalMode(false);

	puts("calling read() with canonical mode off");

	// Now read() will return only when the buffer is filled. The
	// buffer is written to directly for each keypress.
	bytes_read = read(STDIN_FILENO, buf, sizeof(buf));

	if (bytes_read == -1)
	{
		perror("read");
		return EXIT_FAILURE;
	}

	if (bytes_read != 3)
	{
		fprintf(stderr, "bytes_read (%d) != %d\n", bytes_read, sizeof(buf));
		return EXIT_FAILURE;
	}

	printf("buf: '%.*s'\ntest finished\n", sizeof(buf), buf);
}
