/*
 * snprintf.h
 *   header file for snprintf.c
 *
 * $Id$
 */

#ifndef _EGG_COMPAT_SNPRINTF_H_
#define _EGG_COMPAT_SNPRINTF_H_

#include "src/main.h"
#include <stdio.h>

/* Use the system libraries version of vsnprintf() if available. Otherwise
 * use our own.
 */
#ifndef HAVE_VSNPRINTF
int egg_vsnprintf(char *str, size_t count, const char *fmt, va_list ap);
#else
#  define egg_vsnprintf	vsnprintf
#endif

/* Use the system libraries version of snprintf() if available. Otherwise
 * use our own.
 */
#ifndef HAVE_SNPRINTF
#  ifdef __STDC__
int egg_snprintf(char *str, size_t count, const char *fmt, ...);
#  else
int egg_snprintf();
#  endif
#else
#  define egg_snprintf	snprintf
#endif

#endif	/* !_EGG_COMPAT_SNPRINTF_H_ */
