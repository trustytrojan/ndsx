#include <stdio-posix.h>

/*
Replace libnds's basic device streams (which only support putc/getc)
with POSIX FILE structs that will use our overridden read()/write() syscalls.
To save memory just like libnds, provide single-char buffers except for stdout.

Also, this must be a C file because FDEV_SETUP_POSIX uses `.field` syntax mixed
with positional fields in a struct literal, which C++ doesn't like.
*/

static char __stdin_buf[1], __stdout_buf[BUFSIZ], __stderr_buf[1];

static struct __file_bufio
	// fully-buffered stdin with 1-char buffer.
	__stdin = FDEV_SETUP_POSIX(0, __stdin_buf, sizeof(__stdin_buf), __SRD, 0),
	// line-buffered stdout.
	__stdout = FDEV_SETUP_POSIX(1, __stdout_buf, sizeof(__stdout_buf), __SWR, __BLBF),
	// line-buffered stderr with 1-char buffer.
	__stderr = FDEV_SETUP_POSIX(2, __stderr_buf, sizeof(__stderr_buf), __SWR, __BLBF);

FILE *const stdin = &__stdin.xfile.cfile.file;
FILE *const stdout = &__stdout.xfile.cfile.file;
FILE *const stderr = &__stderr.xfile.cfile.file;
