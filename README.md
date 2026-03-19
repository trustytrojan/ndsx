# ndsx
An attempt at a POSIX-compliant microkernel for the Nintendo DS, built on top of [BlocksDS](https://blocksds.skylyrac.net/). Currently in its early stages, and playing around with the many possibilities this could have. This is also my first attempt at writing an OS kernel of any kind.

## Impossibilities
Due to the memory constraints of the Nintendo DS (4MB RAM), and the lack of an MMU, true process isolation is impossible. But as a hobby project, security isn't a concern. 

## Challenges and solutions
Linking executables with a C library is impractical due to the in-memory code duplication it causes. But if all processes share the kernel's libc, how are the standard `FILE *` streams kept separate? Better yet, how can processes make basic system calls that the kernel is aware of, if libnds contains functions of the names?

Simple solution: symbol renaming. By using [Wonderful Toolchain](https://codeberg.org/WonderfulToolchain/)'s `objcopy` with `--redefine-syms`, we can rename any symbols in any object files of our choosing. So by renaming libnds's `open` to `libnds_open`, voila! Now we own the `open` symbol and can implement the syscall our own way, calling `libnds_open` when needed.

### Standard `FILE *` streams
The standard `FILE *` streams were a separate challenge. After reading through [Picolibc](https://codeberg.org/WonderfulToolchain/wf-picolibc) code, I figured out that we can replace libnds's standard `FILE` objects, which simply had `putc` and `getc` function pointers, with our own that actually use our system calls. I did, importantly, stick with BlocksDS's philosophy of minimal code size, by keeping the standard `FILE *` streams unbuffered (by giving them a single-character buffer). Of course, we use `objcopy` to do the dirty work that makes this possible.

### Environment variables
This was a special case. After reading a lot of POSIX manpages on the matter, and seeing that Picolibc's `*env` functions are fully capable of working with `extern char **environ`, my idea was to simply swap the value of `environ` immediately before and after yielding a thread. Luckily, libnds's cothread headers define the structure of the internal control block of threads, and the `cothread_t` identifiers are also pointers to the control blocks. Using this, when a thread yields, ndsx is able to (1) save `environ` to the current process's `envp`, (2) set `environ` to the next thread's process, yield, and (3) upon resuming from a yield, restore `environ` back to the current process's `envp`.
