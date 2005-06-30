#include <string.h>
#include "memcpy.h"
#include "src/main.h"
#include <stdlib.h>

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

void 
str_redup(char **str, const char *newstr)
{
        size_t len;

        if (!newstr) {
                if (*str) free(*str);
                *str = NULL;
                return;
        }
        len = strlen(newstr) + 1;
        *str = (char *) my_realloc(*str, len);
        egg_memcpy(*str, newstr, len);
}

char *
strdup(const char *entry)
{
  size_t len = strlen(entry) + 1;
  char *target = (char *) my_calloc(1, len);
  if (target == NULL) return NULL;
  return (char *) egg_memcpy(target, entry, len);
}

void *my_calloc(size_t nmemb, size_t size)
{
  if (segfaulted)
    return NULL;

  void *ptr = calloc(nmemb, size);

  if (ptr == NULL)
    fatal("Cannot allocate memory", 0);
  
  return ptr;
}

void *my_realloc(void *ptr, size_t size)
{
  if (segfaulted)
    return NULL;

  void *x = realloc(ptr, size);

  if (x == NULL && size > 0)
    fatal("Cannot allocate memory", 0);

  return x;
}

