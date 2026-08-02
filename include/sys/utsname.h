#pragma once

#include <sys/cdefs.h>

#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];  /* Operating system name (e.g. "ndsx") */
    char nodename[_UTSNAME_LENGTH]; /* Network node hostname */
    char release[_UTSNAME_LENGTH];  /* OS release version (e.g. "1.0.0") */
    char version[_UTSNAME_LENGTH];  /* OS build version string */
    char machine[_UTSNAME_LENGTH];  /* Hardware architecture */
    char domainname[_UTSNAME_LENGTH];/* NIS/YP domain name */
};

_BEGIN_STD_C

int uname(struct utsname *buf);

_END_STD_C
