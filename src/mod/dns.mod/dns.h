/*
 * dns.h -- part of dns.mod
 *   dns module header file
 *
 * Written by Fabian Knittel <fknittel@gmx.de>
 *
 * $Id$
 */

/*
 * Borrowed from mtr  --  a network diagnostic tool
 * Copyright (C) 1997,1998  Matt Kimball <mkimball@xmission.com>
 * Released under GPL, as above.
 *
 * Non-blocking DNS portion --
 * Copyright (C) 1998  Simon Kirby <sim@neato.org>
 * Released under GPL, as above.
 */

#ifndef _EGG_MOD_DNS_DNS_H
#define _EGG_MOD_DNS_DNS_H

#include "src/common.h"
#include "src/types.h"

struct resolve {
    struct resolve	*next;
    struct resolve	*previous;
    struct resolve	*nextid;
    struct resolve	*previousid;
    struct resolve	*nextip;
    struct resolve	*previousip;
    struct resolve	*nexthost;
    struct resolve	*previoushost;
    time_t		expiretime;
    char		*hostn;
    in_addr_t			ip;
    u_16bit_t		id;
    u_8bit_t		state;
    u_8bit_t		sends;
};

enum resolve_states {
    STATE_FINISHED,
    STATE_FAILED,
    STATE_PTRREQ,
    STATE_AREQ
};

#define IS_PTR(x) (x->state == STATE_PTRREQ)
#define IS_A(x)   (x->state == STATE_AREQ)

#define DEBUG_DNS 1

#define ddebug0(x) sdprintf(x)
#define ddebug1(x, x1) sdprintf(x, x1)
#define ddebug2(x, x1, x2) sdprintf(x, x1, x2)
#define ddebug3(x, x1, x2, x3) sdprintf(x, x1, x2, x3)
#define ddebug4(x, x1, x2, x3, x4) sdprintf(x, x1, x2, x3, x4)

int dns_report(int, int);
void dns_hostbyip(in_addr_t);
void dns_ipbyhost(char *);

#endif	/* _EGG_MOD_DNS_DNS_H */
