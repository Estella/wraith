#ifndef _SPRINTFH_H
#define _SPRINTF_H

/* $Id$ */

#include <sys/types.h>

#define VSPRINTF_SIZE		2047

size_t simple_sprintf (char *, const char *, ...);
size_t simple_snprintf (char *, size_t, const char *, ...);
//char *int_to_base10(int);
//char *unsigned_int_to_base10(unsigned int);
//char *int_to_base64(unsigned int);

#endif /* _SPRINTF_H */
