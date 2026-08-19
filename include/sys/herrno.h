#pragma once

#include <sys/cdefs.h>

_BEGIN_STD_C

extern int h_errno;
const char *hstrerror(int err);

_END_STD_C
