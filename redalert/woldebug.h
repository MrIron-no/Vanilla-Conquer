//
// Debug output macros for the Westwood Online lobby code.
//
// The original routed these to the Win95 debugger. Here they compile to
// nothing unless WOL_DEBUG is defined, in which case they print to stderr.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifndef WOLDEBUG_H
#define WOLDEBUG_H

#include <stdio.h>

#ifdef WOL_DEBUG
#define debugprint(...) fprintf(stderr, __VA_ARGS__)
#define debugprogress   fprintf(stderr, "...%s: %i\n", __FILE__, __LINE__)
#define _ASSERTE(x)                                                                                                    \
    if (!(x))                                                                                                          \
    fprintf(stderr, "ASSERT FALSE at %s:%i\n", __FILE__, __LINE__)
#else
#define debugprint(...) ((void)0)
#define debugprogress   ((void)0)
#define _ASSERTE(x)     ((void)0)
#endif

#endif // WOLDEBUG_H
