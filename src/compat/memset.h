/*
 * memset.h
 *   prototypes for memset.c
 *
 * $Id$
 */

#ifndef _EGG_COMPAT_MEMSET_H
#define _EGG_COMPAT_MEMSET_H

#include "src/main.h"
#include <string.h>

#ifndef HAVE_MEMSET
/* Use our own implementation. */
void *egg_memset(void *dest, int c, size_t n);
#else
#  define egg_memset	memset
#endif

/* Use memset instead of bzero.
 */
#define egg_bzero(dest, n)	egg_memset(dest, 0, n)

#endif	/* !__EGG_COMPAT_MEMSET_H */
