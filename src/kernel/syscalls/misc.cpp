#include <errno.h>
#include <sys/types.h>

extern "C"
{
int getgroups(int gidsetsize, gid_t grouplist[])
{
	// POSIX requirement: If gidsetsize is negative, it's an invalid argument
	if (gidsetsize < 0)
	{
		errno = EINVAL;
		return -1;
	}

	// POSIX requirement: If gidsetsize is 0, return the number of
	// supplementary group IDs without modifying the grouplist array.
	if (gidsetsize == 0)
		return 0;

	// If gidsetsize > 0, we would normally fill grouplist.
	// Since we have 0 groups, we write nothing to the array and return 0.
	return 0;
}
}
