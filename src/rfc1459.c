/* 
 * rfc1459.c
 * 
 * $Id$
 */
/* 
 * Copyright (C) 1990 Jarkko Oikarinen
 * Copyright (C) 1999, 2000, 2001, 2002 Eggheads Development Team
 * 
 * This code was more or less cloned from the ircd-hybrid 5.3 source.
 * The original code was written by Otto Harkoonen and even though it
 * it not entirely in synch with section 2.2 of RFC1459 in that it
 * additionally defines carat as an uppercase version of tilde, it's
 * what is in the servers themselves so we're going with it this way.
 * 
 * If for some reason someone who maintains the source for ircd decides
 * to change the code to be completely RFC compliant, the change here
 * would be absolutely miniscule.
 * 
 * BTW, since carat characters are allowed in nicknames and tildes are
 * not, I stronly suggest that people convert to uppercase when doing
 * comparisons or creation of hash elements (which tcl laughably calls
 * arrays) to avoid making entries with impossible nicknames in them.
 * 
 * --+ Dagmar
 */

#include "eggmain.h"

int _rfc_casecmp(const char *s1, const char *s2)
{
  register unsigned char *str1 = (unsigned char *) s1;
  register unsigned char *str2 = (unsigned char *) s2;
  register int res;

  while (!(res = rfc_toupper(*str1) - rfc_toupper(*str2))) {
    if (*str1 == '\0')
      return 0;
    str1++;
    str2++;
  }
  return (res);
}

int _rfc_ncasecmp(const char *str1, const char *str2, int n)
{
  register unsigned char *s1 = (unsigned char *) str1;
  register unsigned char *s2 = (unsigned char *) str2;
  register int res;

  while (!(res = rfc_toupper(*s1) - rfc_toupper(*s2))) {
    s1++;
    s2++;
    n--;
    if (!n || (*s1 == '\0' && *s2 == '\0'))
      return 0;
  }
  return (res);
}

unsigned char rfc_tolowertab[];
unsigned char rfc_touppertab[];

int _rfc_tolower(int c)
{
  return rfc_tolowertab[(unsigned char)(c)];
}

int _rfc_toupper(int c)
{
  return rfc_touppertab[(unsigned char)(c)];
}

unsigned char rfc_tolowertab[] =
{0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,
 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
 0x1e, 0x1f,
 ' ', '!', '"', '#', '$', '%', '&', 0x27, '(', ')',
 '*', '+', ',', '-', '.', '/',
 '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
 ':', ';', '<', '=', '>', '?',
 '@', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~',
 '_',
 '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~',
 0x7f,
 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};

unsigned char rfc_touppertab[] =
{0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,
 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
 0x1e, 0x1f,
 ' ', '!', '"', '#', '$', '%', '&', 0x27, '(', ')',
 '*', '+', ',', '-', '.', '/',
 '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
 ':', ';', '<', '=', '>', '?',
 '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', '\\', ']', '^',
 0x5f,
 '`', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', '\\', ']', '^',
 0x7f,
 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};

