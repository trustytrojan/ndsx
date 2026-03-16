#include <stdio-posix.h>

/*
Replace libnds's basic device streams (which only support putc/getc)
with POSIX FILE structs that will use our overridden read()/write() syscalls.

Also, this must be a C file because FDEV_SETUP_POSIX uses `.field` syntax mixed
with positional fields in a braced initializer, which C++ doesn't like.
*/

// A 1-character buffer make a stream essentially unbuffered.
// This is also what `setvbuf(_IONBF)` does.
static char __stdin_buf[1], __stdout_buf[1], __stderr_buf[1];

static struct __file_bufio __stdin = FDEV_SETUP_POSIX(0, __stdin_buf, sizeof(__stdin_buf), __SRD, 0),
						   __stdout = FDEV_SETUP_POSIX(1, __stdout_buf, sizeof(__stdout_buf), __SWR, __BLBF),
						   __stderr = FDEV_SETUP_POSIX(2, __stderr_buf, sizeof(__stderr_buf), __SWR, __BLBF);

FILE *const stdin = &__stdin.xfile.cfile.file;
FILE *const stdout = &__stdout.xfile.cfile.file;
FILE *const stderr = &__stderr.xfile.cfile.file;
