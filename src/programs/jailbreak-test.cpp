// this program will compile and link with dsltool just fine,
// but when loaded by ndsx will fail since ndsx's symbol resolver
// rejects all symbols prefixed with `libnds_`.

#include <stdio.h>

extern "C" void libnds_cothread_yield();

int main()
{
	puts("jailbreak-test: executed");
	libnds_cothread_yield();
}
