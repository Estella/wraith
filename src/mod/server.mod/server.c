#ifdef LEAF
/*
 * server.c -- part of server.mod
 *   basic irc server support
 *
 * $Id$
 */

#include "src/common.h"
#include "src/cfg.h"
#include "src/botmsg.h"
#include "src/rfc1459.h"
#include "src/settings.h"
#include "src/tclhash.h"
#include "src/users.h"
#include "src/userrec.h"
#include "src/main.h"
#include "src/misc.h"
#include "src/chanprog.h"
#include "src/net.h"
#include "src/auth.h"
#include "src/dns.h"
#include "src/egg_timer.h"
#include "src/mod/channels.mod/channels.h"
#include "server.h"

extern struct cfg_entry CFG_OPTIMESLACK;

int checked_hostmask;	/* Used in request_op()/check_hostmask() cleared on connect */
int ctcp_mode;
int serv;		/* sock # of server currently */
int servidx;		/* idx of server */
char newserver[121] = "";	/* new server? */
int newserverport;		/* new server port? */
char newserverpass[121] = "";	/* new server password? */
static time_t trying_server;	/* trying to connect to a server right now? */
static int curserv;		/* current position in server list: */
int flud_thr;		/* msg flood threshold */
int flud_time;		/* msg flood time */
int flud_ctcp_thr;	/* ctcp flood threshold */
int flud_ctcp_time;	/* ctcp flood time */
char botuserhost[121] = "";	/* bot's user@host (refreshed whenever the
				   bot joins a channel) */
				/* may not be correct user@host BUT it's
				   how the server sees it */
static int keepnick;		/* keep trying to regain my intended
				   nickname? */
static int nick_juped = 0;	/* True if origbotname is juped(RPL437) (dw) */
static int check_stoned;	/* Check for a stoned server? */
static int serverror_quit;	/* Disconnect from server if ERROR
				   messages received? */
int quiet_reject;	/* Quietly reject dcc chat or sends from
				   users without access? */
static int waiting_for_awake;	/* set when i unidle myself, cleared when
				   i get the response */
time_t server_online;	/* server connection time */
static time_t server_cycle_wait;	/* seconds to wait before
					   re-beginning the server list */
char botrealname[121] = "";	/* realname of bot */
static int server_timeout;	/* server timeout for connecting */
static int never_give_up;	/* never give up when connecting to servers? */
static int strict_servernames;	/* don't update server list */
static struct server_list *serverlist = NULL;	/* old-style queue, still used by
					   server list */
int cycle_time;			/* cycle time till next server connect */
port_t default_port;		/* default IRC port */
static char oldnick[NICKLEN] = "";	/* previous nickname *before* rehash */
int trigger_on_ignore;	/* trigger bindings if user is ignored ? */
int answer_ctcp;		/* answer how many stacked ctcp's ? */
static int lowercase_ctcp;	/* answer lowercase CTCP's (non-standard) */
static int check_mode_r;	/* check for IRCNET +r modes */
static int net_type;
static int resolvserv;		/* in the process of resolving a server host */
static int double_mode;		/* allow a msgs to be twice in a queue? */
static int double_server;
static int double_help;
static int double_warned;
static int lastpingtime;	/* IRCNet LAGmeter support -- drummer */
static char stackablecmds[511] = "";
static char stackable2cmds[511] = "";
static time_t last_time;
static int use_penalties;
static int use_fastdeq;
int nick_len;			/* Maximal nick length allowed on the network. */
static int kick_method;
static int optimize_kicks;


static void empty_msgq(void);
static void next_server(int *, char *, port_t *, char *);
static void disconnect_server(int, int);
static int calc_penalty(char *);
static int fast_deq(int);
static char *splitnicks(char **);
static void check_queues(char *, char *);
static void parse_q(struct msgq_head *, char *, char *);
static void purge_kicks(struct msgq_head *);
static int deq_kick(int);
static void msgq_clear(struct msgq_head *qh);
static int stack_limit;

/* New bind tables. */
static bind_table_t *BT_raw = NULL, *BT_msg = NULL;
bind_table_t *BT_ctcr = NULL, *BT_ctcp = NULL;
#ifdef S_AUTHCMDS
bind_table_t *BT_msgc = NULL;
#endif /* S_AUTHCMDS */

#include "servmsg.c"

#define MAXPENALTY 10

/* Number of seconds to wait between transmitting queued lines to the server
 * lower this value at your own risk.  ircd is known to start flood control
 * at 512 bytes/2 seconds.
 */
#define msgrate 2

/* Maximum messages to store in each queue. */
static int maxqmsg;
static struct msgq_head mq, hq, modeq;
static int burst;

#include "cmdsserv.c"


/*
 *     Bot server queues
 */

/* Called periodically to shove out another queued item.
 *
 * 'mode' queue gets priority now.
 *
 * Most servers will allow 'busts' of upto 5 msgs, so let's put something
 * in to support flushing modeq a little faster if possible.
 * Will send upto 4 msgs from modeq, and then send 1 msg every time
 * it will *not* send anything from hq until the 'burst' value drops
 * down to 0 again (allowing a sudden mq flood to sneak through).
 */
static void deq_msg()
{
  struct msgq *q = NULL;
  int ok = 0;

  /* now < last_time tested 'cause clock adjustments could mess it up */
  if ((now - last_time) >= msgrate || now < (last_time - 90)) {
    last_time = now;
    if (burst > 0)
      burst--;
    ok = 1;
  }
  if (serv < 0)
    return;
  /* Send upto 4 msgs to server if the *critical queue* has anything in it */
  if (modeq.head) {
    while (modeq.head && (burst < 4) && ((last_time - now) < MAXPENALTY)) {
      if (deq_kick(DP_MODE)) {
        burst++;
        continue;
      }
      if (!modeq.head)
        break;
      if (fast_deq(DP_MODE)) {
        burst++;
        continue;
      }
      tputs(serv, modeq.head->msg, modeq.head->len);
      if (debug_output) {
	modeq.head->msg[strlen(modeq.head->msg) - 1] = 0; /* delete the "\n" */
        putlog(LOG_SRVOUT, "@", "[m->] %s", modeq.head->msg);
      }
      modeq.tot--;
      last_time += calc_penalty(modeq.head->msg);
      q = modeq.head->next;
      free(modeq.head->msg);
      free(modeq.head);
      modeq.head = q;
      burst++;
    }
    if (!modeq.head)
      modeq.last = 0;
    return;
  }
  /* Send something from the normal msg q even if we're slightly bursting */
  if (burst > 1)
    return;
  if (mq.head) {
    burst++;
    if (deq_kick(DP_SERVER))
      return;
    if (fast_deq(DP_SERVER))
      return;
    tputs(serv, mq.head->msg, mq.head->len);
    if (debug_output) {
      mq.head->msg[strlen(mq.head->msg) - 1] = 0; /* delete the "\n" */
      putlog(LOG_SRVOUT, "@", "[s->] %s", mq.head->msg);
    }
    mq.tot--;
    last_time += calc_penalty(mq.head->msg);
    q = mq.head->next;
    free(mq.head->msg);
    free(mq.head);
    mq.head = q;
    if (!mq.head)
      mq.last = NULL;
    return;
  }
  /* Never send anything from the help queue unless everything else is
   * finished.
   */
  if (!hq.head || burst || !ok)
    return;
  if (deq_kick(DP_HELP))
    return;
  if (fast_deq(DP_HELP))
    return;
  tputs(serv, hq.head->msg, hq.head->len);
  if (debug_output) {
    hq.head->msg[strlen(hq.head->msg) - 1] = 0; /* delete the "\n" */
    putlog(LOG_SRVOUT, "*", "[h->] %s", hq.head->msg);
  }
  hq.tot--;
  last_time += calc_penalty(hq.head->msg);
  q = hq.head->next;
  free(hq.head->msg);
  free(hq.head);
  hq.head = q;
  if (!hq.head)
    hq.last = NULL;
}

static int calc_penalty(char * msg)
{
  char *cmd = NULL, *par1 = NULL, *par2 = NULL, *par3 = NULL;
  register int penalty, i, ii;

  if (!use_penalties &&
      net_type != NETT_UNDERNET && net_type != NETT_HYBRID_EFNET)
    return 0;
  if (msg[strlen(msg) - 1] == '\n')
    msg[strlen(msg) - 1] = '\0';
  cmd = newsplit(&msg);
  if (msg)
    i = strlen(msg);
  else
    i = strlen(cmd);
  last_time -= 2; /* undo eggdrop standard flood prot */
  if (net_type == NETT_UNDERNET || net_type == NETT_HYBRID_EFNET) {
    last_time += (2 + i / 120);
    return 0;
  }
  penalty = (1 + i / 100);
  if (!egg_strcasecmp(cmd, "KICK")) {
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* victim(s) */
    par3 = splitnicks(&par2);
    penalty++;
    while (strlen(par3) > 0) {
      par3 = splitnicks(&par2);
      penalty++;
    }
    ii = penalty;
    par3 = splitnicks(&par1);
    while (strlen(par1) > 0) {
      par3 = splitnicks(&par1);
      penalty += ii;
    }
  } else if (!egg_strcasecmp(cmd, "MODE")) {
    i = 0;
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* mode(s) */
    if (!strlen(par2))
      i++;
    while (strlen(par2) > 0) {
      if (strchr("ntimps", par2[0]))
        i += 3;
      else if (!strchr("+-", par2[0]))
        i += 1;
      par2++;
    }
    while (strlen(msg) > 0) {
      newsplit(&msg);
      i += 2;
    }
    ii = 0;
    while (strlen(par1) > 0) {
      splitnicks(&par1);
      ii++;
    }
    penalty += (ii * i);
  } else if (!egg_strcasecmp(cmd, "TOPIC")) {
    penalty++;
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* topic */
    if (strlen(par2) > 0) {  /* topic manipulation => 2 penalty points */
      penalty += 2;
      par3 = splitnicks(&par1);
      while (strlen(par1) > 0) {
        par3 = splitnicks(&par1);
        penalty += 2;
      }
    }
  } else if (!egg_strcasecmp(cmd, "PRIVMSG") ||
	     !egg_strcasecmp(cmd, "NOTICE")) {
    par1 = newsplit(&msg); /* channel(s)/nick(s) */
    /* Add one sec penalty for each recipient */
    while (strlen(par1) > 0) {
      splitnicks(&par1);
      penalty++;
    }
  } else if (!egg_strcasecmp(cmd, "WHO")) {
    par1 = newsplit(&msg); /* masks */
    par2 = par1;
    while (strlen(par1) > 0) {
      par2 = splitnicks(&par1);
      if (strlen(par2) > 4)   /* long WHO-masks receive less penalty */
        penalty += 3;
      else
        penalty += 5;
    }
  } else if (!egg_strcasecmp(cmd, "AWAY")) {
    if (strlen(msg) > 0)
      penalty += 2;
    else
      penalty += 1;
  } else if (!egg_strcasecmp(cmd, "INVITE")) {
    /* Successful invite receives 2 or 3 penalty points. Let's go
     * with the maximum.
     */
    penalty += 3;
  } else if (!egg_strcasecmp(cmd, "JOIN")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "PART")) {
    penalty += 4;
  } else if (!egg_strcasecmp(cmd, "VERSION")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "TIME")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "TRACE")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "NICK")) {
    penalty += 3;
  } else if (!egg_strcasecmp(cmd, "ISON")) {
    penalty += 1;
  } else if (!egg_strcasecmp(cmd, "WHOIS")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "DNS")) {
    penalty += 2;
  } else
    penalty++; /* just add standard-penalty */
  /* Shouldn't happen, but you never know... */
  if (penalty > 99)
    penalty = 99;
  if (penalty < 2) {
    putlog(LOG_SRVOUT, "*", "Penalty < 2sec, that's impossible!");
    penalty = 2;
  }
  if (debug_output && penalty != 0)
    putlog(LOG_SRVOUT, "*", "Adding penalty: %i", penalty);
  return penalty;
}

char *splitnicks(char **rest)
{
  register char *o = NULL, *r = NULL;

  if (!rest)
    return *rest = "";
  o = *rest;
  while (*o == ' ')
    o++;
  r = o;
  while (*o && *o != ',')
    o++;
  if (*o)
    *o++ = 0;
  *rest = o;
  return r;
}

static int fast_deq(int which)
{
  struct msgq_head *h = NULL;
  struct msgq *m = NULL, *nm = NULL;
  char msgstr[511] = "", nextmsgstr[511] = "", tosend[511] = "", victims[511] = "", stackable[511] = "",
       *msg = NULL, *nextmsg = NULL, *cmd = NULL, *nextcmd = NULL, *to = NULL, *nextto = NULL, *stckbl = NULL;
  int len, doit = 0, found = 0, cmd_count = 0, stack_method = 1;

  if (!use_fastdeq)
    return 0;
  switch (which) {
    case DP_MODE:
      h = &modeq;
      break;
    case DP_SERVER:
      h = &mq;
      break;
    case DP_HELP:
      h = &hq;
      break;
    default:
      return 0;
  }
  m = h->head;
  strncpyz(msgstr, m->msg, sizeof msgstr);
  msg = msgstr;
  cmd = newsplit(&msg);
  if (use_fastdeq > 1) {
    strncpyz(stackable, stackablecmds, sizeof stackable);
    stckbl = stackable;
    while (strlen(stckbl) > 0)
      if (!egg_strcasecmp(newsplit(&stckbl), cmd)) {
        found = 1;
        break;
      }
    /* If use_fastdeq is 2, only commands in the list should be stacked. */
    if (use_fastdeq == 2 && !found)
      return 0;
    /* If use_fastdeq is 3, only commands that are _not_ in the list
     * should be stacked.
     */
    if (use_fastdeq == 3 && found)
      return 0;
    /* we check for the stacking method (default=1) */
    strncpyz(stackable, stackable2cmds, sizeof stackable);
    stckbl = stackable;
    while (strlen(stckbl) > 0)
      if (!egg_strcasecmp(newsplit(&stckbl), cmd)) {
        stack_method = 2;
        break;
      }    
  }
  to = newsplit(&msg);
  len = strlen(to);
  if (to[len - 1] == '\n')
    to[len -1] = 0;
  simple_sprintf(victims, "%s", to);
  while (m) {
    nm = m->next;
    if (!nm)
      break;
    strncpyz(nextmsgstr, nm->msg, sizeof nextmsgstr);
    nextmsg = nextmsgstr;
    nextcmd = newsplit(&nextmsg);
    nextto = newsplit(&nextmsg);
    len = strlen(nextto);
    if (nextto[len - 1] == '\n')
      nextto[len - 1] = 0;
    if ( strcmp(to, nextto) /* we don't stack to the same recipients */
        && !strcmp(cmd, nextcmd) && !strcmp(msg, nextmsg)
        && ((strlen(cmd) + strlen(victims) + strlen(nextto)
	     + strlen(msg) + 2) < 510)
        && (!stack_limit || cmd_count < stack_limit - 1)) {
      cmd_count++;
      if (stack_method == 1)
      	simple_sprintf(victims, "%s,%s", victims, nextto);
      else
      	simple_sprintf(victims, "%s %s", victims, nextto);
      doit = 1;
      m->next = nm->next;
      if (!nm->next)
        h->last = m;
      free(nm->msg);
      free(nm);
      h->tot--;
    } else
      m = m->next;
  }
  if (doit) {
    simple_sprintf(tosend, "%s %s %s", cmd, victims, msg);
    len = strlen(tosend);
    tosend[len - 1] = '\n';
    tputs(serv, tosend, len);
    m = h->head->next;
    free(h->head->msg);
    free(h->head);
    h->head = m;
    if (!h->head)
      h->last = 0;
    h->tot--;
    if (debug_output) {
      tosend[len - 1] = 0;
      switch (which) {
        case DP_MODE:
          putlog(LOG_SRVOUT, "*", "[m=>] %s", tosend);
          break;
        case DP_SERVER:
          putlog(LOG_SRVOUT, "*", "[s=>] %s", tosend);
          break;
        case DP_HELP:
          putlog(LOG_SRVOUT, "*", "[h=>] %s", tosend);
          break;
      }
    }
    last_time += calc_penalty(tosend);
    return 1;
  }
  return 0;
}

static void check_queues(char *old_nick, char *newnick)
{
  if (optimize_kicks == 2) {
    if (modeq.head)
      parse_q(&modeq, old_nick, newnick);
    if (mq.head)
      parse_q(&mq, old_nick, newnick);
    if (hq.head)
      parse_q(&hq, old_nick, newnick);
  }
}

static void parse_q(struct msgq_head *q, char *old_nick, char *newnick)
{
  struct msgq *m = NULL, *lm = NULL;  char buf[511] = "", *msg = NULL, *nicks = NULL, 
              *nick = NULL, *chan = NULL, newnicks[511] = "", newmsg[511] = "";
  int changed;

  for (m = q->head; m;) {
    changed = 0;
    if (optimize_kicks == 2 && !egg_strncasecmp(m->msg, "KICK ", 5)) {
      newnicks[0] = 0;
      strncpyz(buf, m->msg, sizeof buf);
      if (buf[0] && (buf[strlen(buf)-1] == '\n'))
        buf[strlen(buf)-1] = '\0';
      msg = buf;
      newsplit(&msg);
      chan = newsplit(&msg);
      nicks = newsplit(&msg);
      while (strlen(nicks) > 0) {
        nick = splitnicks(&nicks);
        if (!egg_strcasecmp(nick, old_nick) &&
            ((9 + strlen(chan) + strlen(newnicks) + strlen(newnick) +
              strlen(nicks) + strlen(msg)) < 510)) {
          if (newnick)
            egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, newnick);
          changed = 1;
        } else
          egg_snprintf(newnicks, sizeof newnicks, ",%s", nick);
      }
      egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan,
		   newnicks + 1, msg);
    }
    if (changed) {
      if (newnicks[0] == 0) {
        if (!lm)
          q->head = m->next;
        else
          lm->next = m->next;
        free(m->msg);
        free(m);
        m = lm;
        q->tot--;
        if (!q->head)
          q->last = 0;
      } else {
        free(m->msg);
        m->len = strlen(newmsg);
        m->msg = strdup(newmsg);
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = q->head;
  }
}

static void purge_kicks(struct msgq_head *q)
{
  struct msgq *m, *lm = NULL;
  char buf[511] = "", *reason = NULL, *nicks = NULL, *nick = NULL, *chan = NULL,
       newnicks[511] = "", newmsg[511] = "", chans[511] = "", *chns, *ch;
  int changed, found;
  struct chanset_t *cs = NULL;

  for (m = q->head; m;) {
    if (!egg_strncasecmp(m->msg, "KICK", 4)) {
      newnicks[0] = 0;
      changed = 0;
      strncpyz(buf, m->msg, sizeof buf);
      if (buf[0] && (buf[strlen(buf)-1] == '\n'))
        buf[strlen(buf)-1] = '\0';
      reason = buf;
      newsplit(&reason);
      chan = newsplit(&reason);
      nicks = newsplit(&reason);
      while (strlen(nicks) > 0) {
        found = 0;
        nick = splitnicks(&nicks);
        strncpyz(chans, chan, sizeof chans);
        chns = chans;
        while (strlen(chns) > 0) {
          ch = newsplit(&chns);
          cs = findchan(ch);
          if (!cs)
            continue;
          if (ismember(cs, nick))
            found = 1;
        }
        if (found)
          egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, nick);
        else {
          putlog(LOG_SRVOUT, "*", "%s isn't on any target channel, removing "
		 "kick...", nick);
          changed = 1;
        }
      }
      if (changed) {
        if (newnicks[0] == 0) {
          if (!lm)
            q->head = m->next;
          else
            lm->next = m->next;
          free(m->msg);
          free(m);
          m = lm;
          q->tot--;
          if (!q->head)
            q->last = 0;
        } else {
          free(m->msg);
          egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan,
		       newnicks + 1, reason);
          m->len = strlen(newmsg);
          m->msg = strdup(newmsg);
        }
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = q->head;
  }
}

static int deq_kick(int which)
{
  struct msgq_head *h = NULL;
  struct msgq *msg = NULL, *m = NULL, *lm = NULL;
  char buf[511] = "", buf2[511] = "", *reason2 = NULL, *nicks = NULL, *chan = NULL,
       *chan2 = NULL, *reason = NULL, *nick = NULL, newnicks[511] = "", newnicks2[511] = "",
        newmsg[511] = "";
  int changed = 0, nr = 0;

  if (!optimize_kicks)
    return 0;
  newnicks[0] = 0;
  switch (which) {
    case DP_MODE:
      h = &modeq;
      break;
    case DP_SERVER:
      h = &mq;
      break;
    case DP_HELP:
      h = &hq;
      break;
    default:
      return 0;
  }
  if (egg_strncasecmp(h->head->msg, "KICK", 4))
    return 0;
  if (optimize_kicks == 2) {
    purge_kicks(h);
    if (!h->head)
      return 1;
  }
  if (egg_strncasecmp(h->head->msg, "KICK", 4))
    return 0;
  msg = h->head;
  strncpyz(buf, msg->msg, sizeof buf);
  reason = buf;
  newsplit(&reason);
  chan = newsplit(&reason);
  nicks = newsplit(&reason);
  while (strlen(nicks) > 0) {
    egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks,
		 newsplit(&nicks));
    nr++;
  }
  for (m = msg->next, lm = NULL; m && (nr < kick_method);) {
    if (!egg_strncasecmp(m->msg, "KICK", 4)) {
      changed = 0;
      newnicks2[0] = 0;
      strncpyz(buf2, m->msg, sizeof buf2);
      if (buf2[0] && (buf2[strlen(buf2)-1] == '\n'))
        buf2[strlen(buf2)-1] = '\0';
      reason2 = buf2;
      newsplit(&reason2);
      chan2 = newsplit(&reason2);
      nicks = newsplit(&reason2);
      if (!egg_strcasecmp(chan, chan2) && !egg_strcasecmp(reason, reason2)) {
        while (strlen(nicks) > 0) {
          nick = splitnicks(&nicks);
          if ((nr < kick_method) &&
             ((9 + strlen(chan) + strlen(newnicks) + strlen(nick) +
             strlen(reason)) < 510)) {
            egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, nick);
            nr++;
            changed = 1;
          } else
            egg_snprintf(newnicks2, sizeof newnicks2, "%s,%s", newnicks2, nick);
        }
      }
      if (changed) {
        if (newnicks2[0] == 0) {
          if (!lm)
            h->head->next = m->next;
          else
            lm->next = m->next;
          free(m->msg);
          free(m);
          m = lm;
          h->tot--;
          if (!h->head)
            h->last = 0;
        } else {
          free(m->msg);
          egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan2,
		       newnicks2 + 1, reason);
          m->len = strlen(newmsg);
          m->msg = strdup(newmsg);
        }
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = h->head->next;
  }
  egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan, newnicks + 1, reason);
  tputs(serv, newmsg, strlen(newmsg));
  if (debug_output) {
    newmsg[strlen(newmsg) - 1] = 0;
    switch (which) {
      case DP_MODE:
        putlog(LOG_SRVOUT, "@", "[m->] %s", newmsg);
        break;
      case DP_SERVER:
        putlog(LOG_SRVOUT, "@", "[s->] %s", newmsg);
        break;
      case DP_HELP:
        putlog(LOG_SRVOUT, "@", "[h->] %s", newmsg);
        break;
    }
    debug3("Changed: %d, kick-method: %d, nr: %d", changed, kick_method, nr);
  }
  h->tot--;
  last_time += calc_penalty(newmsg);
  m = h->head->next;
  free(h->head->msg);
  free(h->head);
  h->head = m;
  if (!h->head)
    h->last = 0;
  return 1;
}

/* Clean out the msg queues (like when changing servers).
 */
static void empty_msgq()
{
  msgq_clear(&modeq);
  msgq_clear(&mq);
  msgq_clear(&hq);
  burst = 0;
}

/* Use when sending msgs... will spread them out so there's no flooding.
 */
void queue_server(int which, char *buf, int len)
{
  struct msgq_head *h = NULL, tempq;
  struct msgq *q = NULL, *tq = NULL, *tqq = NULL;
  int doublemsg = 0, qnext = 0;

  /* Don't even BOTHER if there's no server online. */
  if (serv < 0)
    return;
  /* No queue for PING and PONG - drummer */
  if (!egg_strncasecmp(buf, "PING", 4) || !egg_strncasecmp(buf, "PONG", 4)) {
    if (buf[1] == 'I' || buf[1] == 'i')
      lastpingtime = now;	/* lagmeter */
    tputs(serv, buf, len);
    if (debug_output) {
      if (buf[len - 1] == '\n')
        buf[len - 1] = 0;
      putlog(LOG_SRVOUT, "@", "[m->] %s", buf);
    }
    return;
  }

  switch (which) {
  case DP_MODE_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_MODE:
    h = &modeq;
    tempq = modeq;
    if (double_mode)
      doublemsg = 1;
    break;

  case DP_SERVER_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_SERVER:
    h = &mq;
    tempq = mq;
    if (double_server)
      doublemsg = 1;
    break;

  case DP_HELP_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_HELP:
    h = &hq;
    tempq = hq;
    if (double_help)
      doublemsg = 1;
    break;

  default:
    putlog(LOG_MISC, "*", "!!! queuing unknown type to server!!");
    return;
  }

  if (h->tot < maxqmsg) {
    /* Don't queue msg if it's already queued?  */
    if (!doublemsg)
      for (tq = tempq.head; tq; tq = tqq) {
	tqq = tq->next;
	if (!egg_strcasecmp(tq->msg, buf)) {
	  if (!double_warned) {
	    if (buf[len - 1] == '\n')
	      buf[len - 1] = 0;
	    debug1("msg already queued. skipping: %s", buf);
	    double_warned = 1;
	  }
	  return;
	}
      }

    q = calloc(1, sizeof(struct msgq));
    if (qnext)
      q->next = h->head;
    else
      q->next = NULL;
    if (h->head) {
      if (!qnext)
        h->last->next = q;
    } else
      h->head = q;
    if (qnext)
       h->head = q;
    h->last = q;
    q->len = len;
    q->msg = calloc(1, len + 1);
    strncpyz(q->msg, buf, len + 1);
    h->tot++;
    h->warned = 0;
    double_warned = 0;
  } else {
    if (!h->warned) {
      switch (which) {   
	case DP_MODE_NEXT:
 	/* Fallthrough */
	case DP_MODE:
      putlog(LOG_MISC, "*", "!!! OVER MAXIMUM MODE QUEUE");
 	break;
    
	case DP_SERVER_NEXT:
 	/* Fallthrough */
 	case DP_SERVER:
	putlog(LOG_MISC, "*", "!!! OVER MAXIMUM SERVER QUEUE");
	break;
            
	case DP_HELP_NEXT:
	/* Fallthrough */
	case DP_HELP:
	putlog(LOG_MISC, "*", "!!! OVER MAXIMUM HELP QUEUE");
	break;
      }
    }
    h->warned = 1;
  }

  if (debug_output && !h->warned) {
    if (buf[len - 1] == '\n')
      buf[len - 1] = 0;
    switch (which) {
    case DP_MODE:
      putlog(LOG_SRVOUT, "@", "[!m] %s", buf);
      break;
    case DP_SERVER:
      putlog(LOG_SRVOUT, "@", "[!s] %s", buf);
      break;
    case DP_HELP:
      putlog(LOG_SRVOUT, "@", "[!h] %s", buf);
      break;
    case DP_MODE_NEXT:
      putlog(LOG_SRVOUT, "@", "[!!m] %s", buf);
      break;
    case DP_SERVER_NEXT:
      putlog(LOG_SRVOUT, "@", "[!!s] %s", buf);
      break;
    case DP_HELP_NEXT:
      putlog(LOG_SRVOUT, "@", "[!!h] %s", buf);
      break;
    }
  }

  if (which == DP_MODE || which == DP_MODE_NEXT)
    deq_msg();		/* DP_MODE needs to be sent ASAP, flush if
			   possible. */
}

/* Add a new server to the server_list.
 */
static void add_server(char *ss)
{
  struct server_list *x = NULL, *z = NULL;
#ifdef USE_IPV6
  char *r = NULL;
#endif /* USE_IPV6 */
  char *p = NULL, *q = NULL;

  for (z = serverlist; z && z->next; z = z->next);
  while (ss) {
    p = strchr(ss, ',');
    if (p)
      *p++ = 0;
    x = calloc(1, sizeof(struct server_list));

    x->next = 0;
    x->realname = 0;
    x->port = 0;
    if (z)
      z->next = x;
    else
      serverlist = x;
    z = x;
    q = strchr(ss, ':');
    if (!q) {
      x->port = default_port;
      x->pass = 0;
      x->name = strdup(ss);
    } else {
#ifdef USE_IPV6
      if (ss[0] == '[') {
        *ss++;
        q = strchr(ss, ']');
        *q++ = 0; /* intentional */
        r = strchr(q, ':');
        if (!r)
          x->port = default_port;
      }
#endif /* USE_IPV6 */
      *q++ = 0;
      x->name = calloc(1, q - ss);
      strcpy(x->name, ss);
      ss = q;
      q = strchr(ss, ':');
      if (!q) {
	x->pass = 0;
      } else {
	*q++ = 0;
        x->pass = strdup(q);
      }
#ifdef USE_IPV6
      if (!x->port) {
        x->port = atoi(ss);
      }
#else
      x->port = atoi(ss);
#endif /* USE_IPV6 */
    }
    ss = p;
  }
}

/* Clear out the given server_list.
 */
static void clearq(struct server_list *xx)
{
  struct server_list *x = NULL;

  while (xx) {
    x = xx->next;
    if (xx->name)
      free(xx->name);
    if (xx->pass)
      free(xx->pass);
    if (xx->realname)
      free(xx->realname);
    free(xx);
    xx = x;
  }
}

void servers_describe(struct cfg_entry * entry, int idx) {
}
void servers6_describe(struct cfg_entry * entry, int idx) {
}

void servers_changed(struct cfg_entry * entry, char * olddata, int * valid) {
#ifdef LEAF
  char *slist = NULL, *p = NULL;

  if (conf.bot->host6 || conf.bot->ip6) /* we want to use the servers6 entry. */
    return;

  slist = (char *) (entry->ldata ? entry->ldata : (entry->gdata ? entry->gdata : ""));
  if (serverlist) {
    clearq(serverlist);
    serverlist = NULL;
  }
#ifdef S_RANDSERVERS
  shuffle(slist, ",");
#endif /* S_RANDSERVERS */
  p = strdup(slist);
  add_server(p);
  free(p);
#endif /* LEAF */
}

void servers6_changed(struct cfg_entry * entry, char * olddata, int * valid) {
#ifdef LEAF
  char *slist = NULL, *p = NULL;

  if (!conf.bot->host6 && !conf.bot->ip6) /* we probably want to use the normal server list.. */
    return;

  slist = (char *) (entry->ldata ? entry->ldata : (entry->gdata ? entry->gdata : ""));
  if (serverlist) {
    clearq(serverlist);
    serverlist = NULL;
  }
#ifdef S_RANDSERVERS
  shuffle(slist, ",");
#endif /* S_RANDSERVERS */
  p = strdup(slist);
  add_server(p);
  free(p);
#endif /* LEAF */
}

void nick_describe(struct cfg_entry * entry, int idx) {
}

void nick_changed(struct cfg_entry * entry, char * olddata, int * valid) {
#ifdef LEAF
  char *p = NULL;

  if (entry->ldata)
    p = entry->ldata;
  else if (entry->gdata)
    p = entry->gdata;
  else
    p = NULL;
  if (p && p[0])
    strncpyz(origbotname, p, NICKLEN + 1);
  else
    strncpyz(origbotname, conf.bot->nick, NICKLEN + 1);
  if (server_online)
    dprintf(DP_SERVER, "NICK %s\n", origbotname);
#endif /* LEAF */
}

void realname_describe(struct cfg_entry * entry, int idx) {
}

void realname_changed(struct cfg_entry * entry, char * olddata, int * valid) {
#ifdef LEAF
  if (entry->ldata) {
    strncpyz(botrealname, (char *) entry->ldata, 121);
  } else if (entry->gdata) {
    strncpyz(botrealname, (char *) entry->gdata, 121);
  }
#endif /* LEAF */
}

struct cfg_entry CFG_SERVERS = {
  "servers", CFGF_LOCAL | CFGF_GLOBAL, NULL, NULL,
  servers_changed, servers_changed, servers_describe
};

struct cfg_entry CFG_SERVERS6 = {
  "servers6", CFGF_LOCAL | CFGF_GLOBAL, NULL, NULL,
  servers6_changed, servers6_changed, servers6_describe
};

struct cfg_entry CFG_NICK = {
  "nick", CFGF_LOCAL | CFGF_GLOBAL, NULL, NULL,
  nick_changed, nick_changed, nick_describe
};

struct cfg_entry CFG_REALNAME = {
  "realname", CFGF_LOCAL | CFGF_GLOBAL, NULL, NULL,
  realname_changed, realname_changed, realname_describe
};


/* Set botserver to the next available server.
 *
 * -> if (*ptr == -1) then jump to that particular server
 */
static void next_server(int *ptr, char *servname, port_t *port, char *pass)
{
  struct server_list *x = serverlist;
  int i = 0;

  if (x == NULL)
    return;
  /* -1  -->  Go to specified server */
  if (*ptr == (-1)) {
    for (; x; x = x->next) {
      if (x->port == *port) {
	if (!egg_strcasecmp(x->name, servname)) {
	  *ptr = i;
	  return;
	} else if (x->realname && !egg_strcasecmp(x->realname, servname)) {
	  *ptr = i;
	  strncpyz(servname, x->realname, sizeof servname);
	  return;
	}
      }
      i++;
    }
    /* Gotta add it: */
    x = calloc(1, sizeof(struct server_list));

    x->next = 0;
    x->realname = 0;
    x->name = strdup(servname);
    x->port = *port ? *port : default_port;
    if (pass && pass[0]) {
      x->pass = strdup(pass);
    } else
      x->pass = NULL;
    list_append((struct list_type **) (&serverlist), (struct list_type *) x);
    *ptr = i;
    return;
  }
  /* Find where i am and boogie */
  i = (*ptr);
  while (i > 0 && x != NULL) {
    x = x->next;
    i--;
  }
  if (x != NULL) {
    x = x->next;
    (*ptr)++;
  }				/* Go to next server */
  if (x == NULL) {
    x = serverlist;
    *ptr = 0;
  }				/* Start over at the beginning */
  strcpy(servname, x->name);
  *port = x->port ? x->port : default_port;
  if (x->pass)
    strcpy(pass, x->pass);
  else
    pass[0] = 0;
}

static void do_nettype(void)
{
  switch (net_type) {
  case NETT_EFNET:
    check_mode_r = 0;
    nick_len = 9;
    break;
  case NETT_IRCNET:
    check_mode_r = 1;
    use_penalties = 1;
    use_fastdeq = 3;
    nick_len = 9;
    simple_sprintf(stackablecmds, "INVITE AWAY VERSION NICK");
    kick_method = 4;
    break;
  case NETT_UNDERNET:
    check_mode_r = 0;
    use_fastdeq = 2;
    nick_len = 9;
    simple_sprintf(stackablecmds, "PRIVMSG NOTICE TOPIC PART WHOIS USERHOST USERIP ISON");
    simple_sprintf(stackable2cmds, "USERHOST USERIP ISON");
    break;
  case NETT_DALNET:
    check_mode_r = 0;
    use_fastdeq = 2;
    nick_len = 32;
    simple_sprintf(stackablecmds, "PRIVMSG NOTICE PART WHOIS WHOWAS USERHOST ISON WATCH DCCALLOW");
    simple_sprintf(stackable2cmds, "USERHOST ISON WATCH");
    break;
  case NETT_HYBRID_EFNET:
    check_mode_r = 0;
    nick_len = 9;
    break;
  }
}

/*
 *     CTCP DCC CHAT functions
 */

static void dcc_chat_hostresolved(int);

/* This only handles CHAT requests, otherwise it's handled in filesys.
 */
static int ctcp_DCC_CHAT(char *nick, char *from, struct userrec *u, char *object, char *keyword, char *text)
{
  char *action = NULL, *param = NULL, *ip = NULL, *prt = NULL;
  int i, ok;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0};
  
  if (!ischanhub())
    return BIND_RET_LOG;

  action = newsplit(&text);
  param = newsplit(&text);
  ip = newsplit(&text);
  prt = newsplit(&text);
  if (egg_strcasecmp(action, "CHAT") || egg_strcasecmp(object, botname) || !u)
    return BIND_RET_LOG;

  get_user_flagrec(u, &fr, 0);
  ok = 1;
  if (ischanhub() && !glob_chuba(fr))
   ok = 0;
  if (dcc_total == max_dcc) {
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_TOOMANYDCCS1);
    putlog(LOG_MISC, "*", DCC_TOOMANYDCCS2, "CHAT", param, nick, from);
  } else if (!ok) {
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_REFUSED2);
    putlog(LOG_MISC, "*", "%s: %s!%s", ischanhub() ? DCC_REFUSED : DCC_REFUSEDNC, nick, from);
  } else if (u_pass_match(u, "-")) {
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_REFUSED3);
    putlog(LOG_MISC, "*", "%s: %s!%s", DCC_REFUSED4, nick, from);
  } else if (atoi(prt) < 1024 || atoi(prt) > 65535) {
    /* Invalid port */
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s (invalid port)\n", nick, DCC_CONNECTFAILED1);
    putlog(LOG_MISC, "*", "%s: CHAT (%s!%s)", DCC_CONNECTFAILED3, nick, from);
  } else {
    i = new_dcc(&DCC_DNSWAIT, sizeof(struct dns_info));
    if (i < 0) {
      putlog(LOG_MISC, "*", "DCC connection: CHAT (%s!%s)", dcc[i].nick, ip);
      return BIND_RET_BREAK;
    }
#ifdef USE_IPV6
    if (hostprotocol(ip) == AF_INET6 && sockprotocol(dcc[i].sock) == AF_INET6) {
      debug1("ipv6 addr: %s",ip);
      strcpy(dcc[i].addr6,ip);
      debug1("ipv6 addr: %s",dcc[i].addr6);
    } else
#endif /* USE_IPV6 */
      dcc[i].addr = my_atoul(ip);
    dcc[i].port = atoi(prt);
    dcc[i].sock = -1;
    strcpy(dcc[i].nick, u->handle);
    strcpy(dcc[i].host, from);
    dcc[i].timeval = now;
    dcc[i].user = u;
#ifdef USE_IPV6
    if (sockprotocol(dcc[i].sock) != AF_INET6) {
#endif /* USE_IPV6 */
/* remove me? */
      dcc[i].addr = my_atoul(ip);
      dcc[i].u.dns->ip = dcc[i].addr;
      dcc[i].u.dns->dns_type = RES_HOSTBYIP;
      dcc[i].u.dns->dns_success = dcc_chat_hostresolved;
      dcc[i].u.dns->dns_failure = dcc_chat_hostresolved;
      dcc[i].u.dns->type = &DCC_CHAT_PASS;
      dcc_dnshostbyip(dcc[i].addr);
#ifdef USE_IPV6
    } else
      dcc_chat_hostresolved(i); /* Don't try to look it up */
#endif /* USE_IPV6 */
  }
  return BIND_RET_BREAK;
}

static void dcc_chat_hostresolved(int i)
{
  char buf[512] = "", ip[512] = "";
  int ok;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0};

  egg_snprintf(buf, sizeof buf, "%d", dcc[i].port);
#ifdef USE_IPV6
  if (sockprotocol(dcc[i].sock) == AF_INET6) {
    strcpy(ip, dcc[i].addr6); /* safe, addr6 is 121 */
  } else
#endif /* !USE_IPV6 */
  egg_snprintf(ip, sizeof ip, "%lu", iptolong(htonl(dcc[i].addr)));
#ifdef USE_IPV6
  if (sockprotocol(dcc[i].sock) == AF_INET6) {
#  ifdef IPV6_DEBUG
    debug2("af_inet6 %s / %s", dcc[i].addr6, ip);
#  endif /* IPV6_DEBUG */
    dcc[i].sock = getsock(0, AF_INET6);
  } else {
#  ifdef IPV6_DEBUG
    debug0("af_inet");
#  endif /* IPV6_DEBUG */
    dcc[i].sock = getsock(0, AF_INET);
  }
#else
  dcc[i].sock = getsock(0);
#  ifdef IPV6_DEBUG
  debug2("sock: %d %s", dcc[i].sock, ip);
#  endif /* IPV6_DEBUG */
#endif /* USE_IPV6 */
  if (dcc[i].sock < 0 || open_telnet_dcc(dcc[i].sock, ip, buf) < 0) {
    neterror(buf);
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s (%s)\n", dcc[i].nick, DCC_CONNECTFAILED1, buf);
    putlog(LOG_MISC, "*", "%s: CHAT (%s!%s)", DCC_CONNECTFAILED2, dcc[i].nick, dcc[i].host);
    putlog(LOG_MISC, "*", "    (%s)", buf);
    killsock(dcc[i].sock);
    lostdcc(i);
  } else {
#ifdef HAVE_SSL
    ssl_link(dcc[i].sock, CONNECT_SSL);
#endif /* HAVE_SSL */
    changeover_dcc(i, &DCC_CHAT_PASS, sizeof(struct chat_info));
    dcc[i].status = STAT_ECHO;
    get_user_flagrec(dcc[i].user, &fr, 0);
    ok = 1;
    if (ischanhub() && !glob_chuba(fr))
     ok = 0;
    if (ok)
      dcc[i].status |= STAT_PARTY;
    strcpy(dcc[i].u.chat->con_chan, (chanset) ? chanset->dname : "*");
    dcc[i].timeval = now;
    /* Ok, we're satisfied with them now: attempt the connect */
    putlog(LOG_MISC, "*", "DCC connection: CHAT (%s!%s)", dcc[i].nick, dcc[i].host);
    dprintf(i, "%s", rand_dccresp());
  }
  return;
}

/*
 *     Server timer functions
 */

static void server_secondly()
{
  if (cycle_time)
    cycle_time--;
  deq_msg();
  if (!resolvserv && serv < 0 && !trying_server)
    connect_server();
}

static void server_5minutely()
{
  if (check_stoned && server_online) {
    if (waiting_for_awake) {
      /* Uh oh!  Never got pong from last time, five minutes ago!
       * Server is probably stoned.
       */
      disconnect_server(servidx, DO_LOST);
      putlog(LOG_SERV, "*", IRC_SERVERSTONED);
    } else if (!trying_server) {
      /* Check for server being stoned. */
      dprintf(DP_MODE, "PING :%li\n", now);
      waiting_for_awake = 1;
    }
  }
}

void server_die()
{
  cycle_time = 100;
  if (server_online) {
    dprintf(-serv, "QUIT :%s\n", quit_msg[0] ? quit_msg : "");
    sleep(2); /* Give the server time to understand */
  }
  nuke_server(NULL);
}

/* A report on the module status.
 */
void server_report(int idx, int details)
{
  char s1[64] = "", s[128] = "";

  if (server_online) {
    dprintf(idx, "    Online as: %s%s%s (%s)\n", botname,
	    botuserhost[0] ? "!" : "", botuserhost[0] ? botuserhost : "",
	    botrealname);
    if (nick_juped)
      dprintf(idx, "    NICK IS JUPED: %s %s\n", origbotname,
	      keepnick ? "(trying)" : "");
    nick_juped = 0; /* WHY?? -- drummer */
    daysdur(now, server_online, s1);
    egg_snprintf(s, sizeof s, "(connected %s)", s1);
    if (server_lag && !waiting_for_awake) {
      if (server_lag == (-1))
	egg_snprintf(s1, sizeof s1, " (bad pong replies)");
      else
	egg_snprintf(s1, sizeof s1, " (lag: %ds)", server_lag);
      strcat(s, s1);
    }
  }
  if ((trying_server || server_online) && (servidx != (-1))) {
    dprintf(idx, "    Server %s:%d %s\n", dcc[servidx].host, dcc[servidx].port,
	    trying_server ? "(trying)" : s);
  } else
    dprintf(idx, "    %s\n", IRC_NOSERVER);
  if (modeq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_MODEQUEUE,
            (int) ((float) (modeq.tot * 100.0) / (float) maxqmsg),
	    (int) modeq.tot);
  if (mq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_SERVERQUEUE,
           (int) ((float) (mq.tot * 100.0) / (float) maxqmsg), (int) mq.tot);
  if (hq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_HELPQUEUE,
           (int) ((float) (hq.tot * 100.0) / (float) maxqmsg), (int) hq.tot);
  if (details) {
    dprintf(idx, "    Flood is: %d msg/%ds, %d ctcp/%ds\n",
	    flud_thr, flud_time, flud_ctcp_thr, flud_ctcp_time);
  }
}

static void msgq_clear(struct msgq_head *qh)
{
  register struct msgq	*q = NULL, *qq = NULL;

  for (q = qh->head; q; q = qq) {
    qq = q->next;
    free(q->msg);
    free(q);
  }
  qh->head = qh->last = NULL;
  qh->tot = qh->warned = 0;
}

static cmd_t my_ctcps[] =
{
  {"DCC",	"",	ctcp_DCC_CHAT,		"server:DCC"},
  {NULL,	NULL,	NULL,			NULL}
};

void server_init()
{
  /*
   * Init of all the variables *must* be done in _start rather than
   * globally.
   */
  serv = -1;
  servidx = -1;
  botname[0] = 0;
  trying_server = 0;
  server_lag = 0;
  curserv = 0;
  flud_thr = 5;
  flud_time = 60;
  flud_ctcp_thr = 3;
  flud_ctcp_time = 60;
  botuserhost[0] = 0;
  keepnick = 1;
  check_stoned = 1;
  serverror_quit = 1;
  quiet_reject = 1;
  waiting_for_awake = 0;
  server_online = 0;
  server_cycle_wait = 15;
  strcpy(botrealname, "A deranged product of evil coders");
  server_timeout = 15;
  never_give_up = 1;
  strict_servernames = 1;
  serverlist = NULL;
  cycle_time = 0;
  oldnick[0] = 0;
  trigger_on_ignore = 0;
  answer_ctcp = 1;
  lowercase_ctcp = 0;
  check_mode_r = 0;
  maxqmsg = 300;
  burst = 0;
  net_type = NETT_EFNET;
  double_mode = 0;
  double_server = 0;
  double_help = 0;
  use_penalties = 0;
  use_fastdeq = 0;
  stackablecmds[0] = 0;
  strcpy(stackable2cmds, "USERHOST ISON");
  resolvserv = 0;
  lastpingtime = 0;
  last_time = 0;
  nick_len = 9;
  kick_method = 1;
  optimize_kicks = 0;
  stack_limit = 4;

#ifdef S_AUTHCMDS
  BT_msgc = bind_table_add("msgc", 5, "ssUss", MATCH_FLAGS, 0); 
#endif /* S_AUTHCMDS */
  BT_msg = bind_table_add("msg", 4, "ssUs", MATCH_FLAGS, 0);
  BT_raw = bind_table_add("raw", 2, "ss", 0, BIND_STACKABLE);

  BT_ctcr = bind_table_add("ctcr", 6, "ssUsss", 0, BIND_STACKABLE);
  BT_ctcp = bind_table_add("ctcp", 6, "ssUsss", 0, BIND_STACKABLE);

  add_builtins("raw", my_raw_binds);
  add_builtins("dcc", C_dcc_serv);
  add_builtins("ctcp", my_ctcps);

  timer_create_secs(1, "server_secondly", (Function) server_secondly);
  timer_create_secs(10, "server_10secondly", (Function) server_10secondly);
  timer_create_secs(300, "server_5minutely", (Function) server_5minutely);
  timer_create_secs(60, "minutely_checks", (Function) minutely_checks);
  mq.head = hq.head = modeq.head = NULL;
  mq.last = hq.last = modeq.last = NULL;
  mq.tot = hq.tot = modeq.tot = 0;
  mq.warned = hq.warned = modeq.warned = 0;
  double_warned = 0;
  newserver[0] = 0;
  newserverport = 0;
  curserv = 999;
  checked_hostmask = 0;
  do_nettype();
  add_cfg(&CFG_NICK);
  add_cfg(&CFG_SERVERS);
  add_cfg(&CFG_SERVERS6);
  add_cfg(&CFG_REALNAME);
    cfg_noshare = 1;
  if (!CFG_REALNAME.gdata)
    set_cfg_str(NULL, "realname", "A deranged product of evil coders.");
  set_cfg_str(NULL, "servport", "6667");
  cfg_noshare = 0;
}
#endif /* LEAF */
