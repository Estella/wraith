/*
 * strftime.h
 *   header file for strftime.c
 *
 * $Id$
 */

#ifndef _EGG_COMPAT_STRFTIME_H_
#define _EGG_COMPAT_STRFTIME_H_

#include "src/eggmain.h"
#include <time.h>

/* Use the system libraries version of strftime() if available. Otherwise
 * use our own.
 */
#ifndef HAVE_STRFTIME
size_t egg_strftime(char *s, size_t maxsize, const char *format,
		    const struct tm *tp);
#else
#  define egg_strftime	strftime
#endif

#endif	/* !_EGG_COMPAT_STRFTIME_H_ */
