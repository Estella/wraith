/*
 * memset.c -- provides memset() if necessary.
 *
 * $Id$
 */

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

#include "common.h"
#include "memset.h"

#ifndef HAVE_MEMSET
void *egg_memset(void *dest, int c, size_t n)
{
  while (n--)
    *((u_8bit_t *) dest)++ = c;
  return dest;
}
#endif /* !HAVE_MEMSET */
