#ifdef LEAF
/*
 * server.h -- part of server.mod
 *
 * $Id$
 */

#ifndef _EGG_MOD_SERVER_SERVER_H
#define _EGG_MOD_SERVER_SERVER_H

#include "src/tclhash.h"

#define DO_LOST 1
#define NO_LOST 0

#define check_bind_ctcp(a,b,c,d,e,f) check_bind_ctcpr(a,b,c,d,e,f,BT_ctcp)
#define check_bind_ctcr(a,b,c,d,e,f) check_bind_ctcpr(a,b,c,d,e,f,BT_ctcr)

#define fixcolon(x)             do {                                    \
        if ((x)[0] == ':')                                              \
                (x)++;                                                  \
        else                                                            \
                (x) = newsplit(&(x));                                   \
} while (0)


struct server_list {
  struct server_list	*next;

  char			*name;
  int			 port;
  char			*pass;
  char			*realname;
};

/* Available net types.  */
enum {
	NETT_EFNET		= 0,	/* EfNet except new +e/+I hybrid. */
	NETT_IRCNET		= 1,	/* Ircnet.			  */
	NETT_UNDERNET		= 2,	/* Undernet.			  */
	NETT_DALNET		= 3,	/* Dalnet.			  */
	NETT_HYBRID_EFNET	= 4	/* new +e/+I Efnet hybrid.	  */
} nett_t;

#define IRC_CANTCHANGENICK "Can't change nickname on %s.  Is my nickname banned?"
#endif		/* _EGG_MOD_SERVER_SERVER_H */

void nuke_server(char *);
int match_my_nick(char *);

extern int 		serv, servidx, cycle_time, default_port, newserverport,
			nick_len, checked_hostmask, ctcp_mode, quiet_reject,
			flud_thr, flud_time, flud_ctcp_thr, flud_ctcp_time,
			answer_ctcp, trigger_on_ignore;
extern time_t		server_online;
extern char		cursrvname[], botrealname[], botuserhost[], ctcp_reply[],
			newserver[], newserverpass[];

int check_bind_ctcpr(char *, char *, struct userrec *, char *, char *, char *, bind_table_t *);
int detect_avalanche(char *);
void server_report(int, int);

#endif /*leaf*/
