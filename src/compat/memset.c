/*
 * memset.c -- provides memset() if necessary.
 *
 * $Id$
 */
#include "eggmain.h"
#include "memset.h"

#ifndef HAVE_MEMSET
void *egg_memset(void *dest, int c, size_t n)
{
  while (n--)
    *((u_8bit_t *) dest)++ = c;
  return dest;
}
#endif /* !HAVE_MEMSET */
