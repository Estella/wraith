/*
 * compat.h
 *   wrap-around header for all compability functions.
 *
 * $Id$
 */

#ifndef _EGG_COMPAT_COMPAT_H
#define _EGG_COMPAT_COMPAT_H

#include "dirname.h"
#include "inet_aton.h"
#include "inet_ntop.h"
#include "snprintf.h"
#include "memset.h"
#include "memcpy.h"
#include "memutil.h"
#include "strcasecmp.h"
#include "strftime.h"


/* These apparently are unsafe without recasting. */
#define egg_isdigit(x)  isdigit((int)  (unsigned char) (x))
#define egg_isxdigit(x) isxdigit((int) (unsigned char) (x))
#define egg_isascii(x)  isascii((int)  (unsigned char) (x))
#define egg_isspace(x)  isspace((int)  (unsigned char) (x))
#define egg_islower(x)  islower((int)  (unsigned char) (x))
#define egg_isupper(x)  isupper((int)  (unsigned char) (x))


#endif /* !__EGG_COMPAT_COMPAT_H */

