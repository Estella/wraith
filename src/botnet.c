/*
 * botnet.c -- handles:
 *   keeping track of which bot's connected where in the chain
 *   dumping a list of bots or a bot tree to a user
 *   channel name associations on the party line
 *   rejecting a bot
 *   linking, unlinking, and relaying to another bot
 *   pinging the bots periodically and checking leaf status
 *
 * $Id$
 */

#include "common.h"
#include "botnet.h"
#include "net.h"
#include "users.h"
#include "misc.h"
#include "userrec.h"
#include "main.h"
#include "dccutil.h"
#include "dcc.h"
#include "botmsg.h"
#include "tandem.h"
#include "core_binds.h"

extern int			dcc_total, backgrd, connect_timeout, max_dcc,
				cfg_count;
extern struct userrec		*userlist;
extern struct dcc_t		*dcc;
extern time_t 			now, buildts;
extern Tcl_Interp		*interp;
extern struct cfg_entry 	**cfg;


tand_t			*tandbot;			/* Keep track of tandem bots on the
							   botnet */
party_t			*party;				/* Keep track of people on the botnet */
static int 		maxparty = 200;			/* Maximum space for party line members
							   currently */
int			tands = 0;			/* Number of bots on the botnet */
int			parties = 0;			/* Number of people on the botnet */
int			share_unlinks = 1;		/* Allow remote unlinks of my
							   sharebots? */

void init_bots()
{
  tandbot = NULL;
  /* Grab space for 50 bots for now -- expand later as needed */
  maxparty = 50;
  party = (party_t *) malloc(maxparty * sizeof(party_t));
}

tand_t *findbot(char *who)
{
  tand_t *ptr;

  for (ptr = tandbot; ptr; ptr = ptr->next)
    if (!egg_strcasecmp(ptr->bot, who))
      return ptr;
  return NULL;
}

/* Add a tandem bot to our chain list
 */
void addbot(char *who, char *from, char *next, char flag, int vernum)
{
  tand_t **ptr = &tandbot, *ptr2;

  while (*ptr) {
    if (!egg_strcasecmp((*ptr)->bot, who))
      putlog(LOG_BOTS, "*", "!!! Duplicate botnet bot entry!!");
    ptr = &((*ptr)->next);
  }
  ptr2 = malloc(sizeof(tand_t));
  strncpy(ptr2->bot, who, HANDLEN);
  ptr2->bot[HANDLEN] = 0;
  ptr2->share = flag;
  ptr2->ver = vernum;
  ptr2->next = *ptr;
  *ptr = ptr2;
  /* May be via itself */
  ptr2->via = findbot(from);
  if (!egg_strcasecmp(next, conf.bot->nick))
    ptr2->uplink = (tand_t *) 1;
  else
    ptr2->uplink = findbot(next);
  tands++;
}

#ifdef HUB
#ifdef G_BACKUP
void check_should_backup()
{
  struct chanset_t *chan;

  for (chan = chanset; chan; chan = chan->next) {
    if (chan->channel.backup_time && (chan->channel.backup_time < now) && !channel_backup(chan)) {
      do_chanset(chan, STR("+backup"), 1);
      chan->channel.backup_time = 0;
    }
  }
}
#endif /* G_BACKUP */
#endif /* HUB */

void updatebot(int idx, char *who, char share, int vernum)
{
  tand_t *ptr = findbot(who);

  if (ptr) {
    if (share)
      ptr->share = share;
    if (vernum)
      ptr->ver = vernum;
    botnet_send_update(idx, ptr);
  }
}

/* For backward 1.0 compatibility:
 * grab the (first) sock# for a user on another bot
 */
int partysock(char *bot, char *nick)
{
  int i;

  for (i = 0; i < parties; i++) {
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(!egg_strcasecmp(party[i].nick, nick)))
      return party[i].sock;
  }
  return 0;
}

/* New botnet member
 */
int addparty(char *bot, char *nick, int chan, char flag, int sock,
	     char *from, int *idx)
{
  int i;

  for (i = 0; i < parties; i++) {
    /* Just changing the channel of someone already on? */
    if (!egg_strcasecmp(party[i].bot, bot) &&
	(party[i].sock == sock)) {
      int oldchan = party[i].chan;

      party[i].chan = chan;
      party[i].timer = now;
      if (from[0]) {
	if (flag == ' ')
	  flag = '-';
	party[i].flag = flag;
	if (party[i].from)
	  free(party[i].from);
        party[i].from = strdup(from);
      }
      *idx = i;
      return oldchan;
    }
  }
  /* New member */
  if (parties == maxparty) {
    maxparty += 50;
    party = (party_t *) realloc((void *) party, maxparty * sizeof(party_t));
  }
  strncpy(party[parties].nick, nick, HANDLEN);
  party[parties].nick[HANDLEN] = 0;
  strncpy(party[parties].bot, bot, HANDLEN);
  party[parties].bot[HANDLEN] = 0;
  party[parties].chan = chan;
  party[parties].sock = sock;
  party[parties].status = 0;
  party[parties].away = 0;
  party[parties].timer = now;	/* cope. */
  if (from[0]) {
    if (flag == ' ')
      flag = '-';
    party[parties].flag = flag;
    party[parties].from = strdup(from);
  } else {
    party[parties].flag = ' ';
    party[parties].from = strdup("(unknown)");
  }
  *idx = parties;
  parties++;
  return -1;
}

/* Alter status flags for remote party-line user.
 */
void partystat(char *bot, int sock, int add, int rem)
{
  int i;

  for (i = 0; i < parties; i++) {
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(party[i].sock == sock)) {
      party[i].status |= add;
      party[i].status &= ~rem;
    }
  }
}

/* Other bot is sharing idle info.
 */
void partysetidle(char *bot, int sock, int secs)
{
  int i;

  for (i = 0; i < parties; i++) {
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(party[i].sock == sock)) {
      party[i].timer = (now - (time_t) secs);
    }
  }
}

/* Return someone's chat channel.
 */
int getparty(char *bot, int sock)
{
  int i;

  for (i = 0; i < parties; i++) {
    if (!egg_strcasecmp(party[i].bot, bot) &&
	(party[i].sock == sock)) {
      return i;
    }
  }
  return -1;
}

/* Un-idle someone
 */
int partyidle(char *bot, char *nick)
{
  int i, ok = 0;

  for (i = 0; i < parties; i++) {
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(!egg_strcasecmp(party[i].nick, nick))) {
      party[i].timer = now;
      ok = 1;
    }
  }
  return ok;
}

/* Change someone's nick
 */
int partynick(char *bot, int sock, char *nick)
{
  char work[HANDLEN + 1];
  int i;

  for (i = 0; i < parties; i++) {
    if (!egg_strcasecmp(party[i].bot, bot) && (party[i].sock == sock)) {
      strcpy(work, party[i].nick);
      strncpy(party[i].nick, nick, HANDLEN);
      party[i].nick[HANDLEN] = 0;
      strcpy(nick, work);
      return i;
    }
  }
  return -1;
}

/* Set away message
 */
void partyaway(char *bot, int sock, char *msg)
{
  int i;

  for (i = 0; i < parties; i++) {
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(party[i].sock == sock)) {
      if (party[i].away)
	free(party[i].away);
      if (msg[0]) {
        party[i].away = strdup(msg);
      } else
	party[i].away = 0;
    }
  }
}

/* Remove a tandem bot from the chain list
 */
void rembot(char *who)
{
  tand_t **ptr = &tandbot, *ptr2;
  struct userrec *u;

  while (*ptr) {
    if (!egg_strcasecmp((*ptr)->bot, who))
      break;
    ptr = &((*ptr)->next);
  }
  if (!*ptr)
    /* May have just .unlink *'d */
    return;
  check_bind_disc(who);

  u = get_user_by_handle(userlist, who);
  if (u != NULL)
    touch_laston(u, "unlinked", now);

  ptr2 = *ptr;
  *ptr = ptr2->next;
  free(ptr2);
  tands--;

  dupwait_notify(who);
}

void remparty(char *bot, int sock)
{
  int i;

  for (i = 0; i < parties; i++)
    if ((!egg_strcasecmp(party[i].bot, bot)) &&
	(party[i].sock == sock)) {
      parties--;
      if (party[i].from)
	free(party[i].from);
      if (party[i].away)
	free(party[i].away);
      if (i < parties) {
	strcpy(party[i].bot, party[parties].bot);
	strcpy(party[i].nick, party[parties].nick);
	party[i].chan = party[parties].chan;
	party[i].sock = party[parties].sock;
	party[i].flag = party[parties].flag;
	party[i].status = party[parties].status;
	party[i].timer = party[parties].timer;
	party[i].from = party[parties].from;
	party[i].away = party[parties].away;
      }
    }
}

/* Cancel every user that was on a certain bot
 */
void rempartybot(char *bot)
{
  int i;

  for (i = 0; i < parties; i++)
    if (!egg_strcasecmp(party[i].bot, bot)) {
      if (party[i].chan >= 0)
        check_bind_chpt(bot, party[i].nick, party[i].sock, party[i].chan);
      remparty(bot, party[i].sock);
      i--;
    }
}

/* Remove every bot linked 'via' bot <x>
 */
void unvia(int idx, tand_t * who)
{
  tand_t *bot, *bot2;

  if (!who)
    return;			/* Safety */
  rempartybot(who->bot);
  bot = tandbot;
  while (bot) {
    if (bot->uplink == who) {
      unvia(idx, bot);
      bot2 = bot->next;
      rembot(bot->bot);
      bot = bot2;
    } else
      bot = bot->next;
  }
}

void besthub(char *hub)
{
  tand_t *ptr = tandbot;
  struct userrec *u, *besthub = NULL;
  char bestlval[20], lval[20];

  hub[0] = 0;
  strcpy(bestlval, "z");
  while (ptr) {
    u = get_user_by_handle(userlist, ptr->bot);
    if (u) {
      link_pref_val(u, lval);
      if (strcmp(lval, bestlval) < 0) {
        strcpy(bestlval, lval);
        besthub = u;
      }
    }
    ptr = ptr->next;
  }
  if (besthub)
    strcpy(hub, besthub->handle);
  return;
}

/* Return index into dcc list of the bot that connects us to bot <x>
 */
int nextbot(char *who)
{
  int j;
  tand_t *bot = findbot(who);

  if (!bot)
    return -1;

  for (j = 0; j < dcc_total; j++)
    if (bot->via && !egg_strcasecmp(bot->via->bot, dcc[j].nick) &&
	(dcc[j].type == &DCC_BOT))
      return j;
  return -1;			/* We're not connected to 'via' */
}

/* Return name of the bot that is directly connected to bot X
 */
char *lastbot(char *who)
{
  tand_t *bot = findbot(who);

  if (!bot)
    return "*";
  else if (bot->uplink == (tand_t *) 1)
    return conf.bot->nick;
  else
    return bot->uplink->bot;
}

/* Modern version of 'whom' (use local data)
 */
void answer_local_whom(int idx, int chan)
{
  char format[81];
  char c, idle[40];
  int i, t, nicklen, botnicklen, total=0;

  if (chan == (-1))
    dprintf(idx, "%s (+: %s, *: %s)\n", BOT_BOTNETUSERS, BOT_PARTYLINE,
	    BOT_LOCALCHAN);
  else if (chan > 0) {
    simple_sprintf(idle, "assoc %d", chan);
    if ((Tcl_Eval(interp, idle) != TCL_OK) || !interp->result[0])
      dprintf(idx, "%s %s%d:\n", BOT_USERSONCHAN,
	      (chan < GLOBAL_CHANS) ? "" : "*", chan % GLOBAL_CHANS);
    else
      dprintf(idx, "%s '%s%s' (%s%d):\n", BOT_USERSONCHAN,
	      (chan < GLOBAL_CHANS) ? "" : "*", interp->result,
	      (chan < GLOBAL_CHANS) ? "" : "*", chan % GLOBAL_CHANS);
  }
  /* Find longest nick and botnick */
  nicklen = botnicklen = 0;
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_CHAT) {
      if ((chan == (-1)) || ((chan >= 0) && (dcc[i].u.chat->channel == chan))) {
        t = strlen(dcc[i].nick); if(t > nicklen) nicklen = t;
        t = strlen(conf.bot->nick); if(t > botnicklen) botnicklen = t;
      }
    }
  for (i = 0; i < parties; i++) {
    if ((chan == (-1)) || ((chan >= 0) && (party[i].chan == chan))) {
      t = strlen(party[i].nick); if(t > nicklen) nicklen = t;
      t = strlen(party[i].bot); if(t > botnicklen) botnicklen = t;
    }
  }
  if(nicklen < 9) nicklen = 9;
  if(botnicklen < 9) botnicklen = 9;

#ifdef HUB
  egg_snprintf(format, sizeof format, "%%-%us   %%-%us  %%s\n", 
                                  nicklen, botnicklen);
  dprintf(idx, format, " Nick", 	" Bot",      " Host");
  dprintf(idx, format, "----------",	"---------", "--------------------");
  egg_snprintf(format, sizeof format, "%%c%%-%us %%c %%-%us  %%s%%s\n", 
                                  nicklen, botnicklen);
#else /* !HUB */
  egg_snprintf(format, sizeof format, "%%-%us\n", nicklen);
  dprintf(idx, format, " Nick");
  dprintf(idx, format, "----------");
  egg_snprintf(format, sizeof format, "%%c%%-%us %%c %%s\n", nicklen);
#endif /* HUB */
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_CHAT) {
      if ((chan == (-1)) || ((chan >= 0) && (dcc[i].u.chat->channel == chan))) {
	c = geticon(i);
	if (c == '-')
	  c = ' ';
	if (now - dcc[i].timeval > 300) {
	  unsigned long days, hrs, mins;

	  days = (now - dcc[i].timeval) / 86400;
	  hrs = ((now - dcc[i].timeval) - (days * 86400)) / 3600;
	  mins = ((now - dcc[i].timeval) - (hrs * 3600)) / 60;
	  if (days > 0)
	    sprintf(idle, " [idle %lud%luh]", days, hrs);
	  else if (hrs > 0)
	    sprintf(idle, " [idle %luh%lum]", hrs, mins);
	  else
	    sprintf(idle, " [idle %lum]", mins);
	} else
	  idle[0] = 0;

        total++;
	dprintf(idx, format, c, dcc[i].nick, 
		(dcc[i].u.chat->channel == 0) && (chan == (-1)) ? '+' :
		(dcc[i].u.chat->channel > GLOBAL_CHANS) &&
#ifdef HUB
		(chan == (-1)) ? '*' : ' ', conf.bot->nick, dcc[i].host, idle);
#else /* !HUB */
		(chan == (-1)) ? '*' : ' ', idle);
#endif /* HUB */
	if (dcc[i].u.chat->away != NULL)
	  dprintf(idx, "   AWAY: %s\n", dcc[i].u.chat->away);
      }
    }
  for (i = 0; i < parties; i++) {
    if ((chan == (-1)) || ((chan >= 0) && (party[i].chan == chan))) {
      c = party[i].flag;
      if (c == '-')
	c = ' ';
      if (party[i].timer == 0L)
	strcpy(idle, " [idle?]");
      else if (now - party[i].timer > 300) {
	unsigned long days, hrs, mins;

	days = (now - party[i].timer) / 86400;
	hrs = ((now - party[i].timer) - (days * 86400)) / 3600;
	mins = ((now - party[i].timer) - (hrs * 3600)) / 60;
	if (days > 0)
	  sprintf(idle, " [idle %lud%luh]", days, hrs);
	else if (hrs > 0)
	  sprintf(idle, " [idle %luh%lum]", hrs, mins);
	else
	  sprintf(idle, " [idle %lum]", mins);
      } else
	idle[0] = 0;
      total++;

      dprintf(idx, format, c, party[i].nick, 
	      (party[i].chan == 0) && (chan == (-1)) ? '+' : ' ',
#ifdef HUB
	      party[i].bot, party[i].from, idle);
#else /* !HUB */
	      idle);
#endif /* HUB */

      if (party[i].status & PLSTAT_AWAY)
	dprintf(idx, "   %s: %s\n", MISC_AWAY,
		party[i].away ? party[i].away : "");
    }
  }
  dprintf(idx, "Total users: %d\n", total);
}

/* Show z a list of all bots connected
 */
void tell_bots(int idx)
{
  char s[512];
  int i;
  tand_t *bot;

  if (!tands) {
    dprintf(idx, STR("No bots linked\n"));
    return;
  }
  strcpy(s, conf.bot->nick);
  i = strlen(conf.bot->nick);

  for (bot = tandbot; bot; bot = bot->next) {
    if (i > (500 - HANDLEN)) {
      dprintf(idx, STR("Bots: %s\n"), s);
      s[0] = 0;
      i = 0;
    }
    if (i) {
      s[i++] = ',';
      s[i++] = ' ';
    }
    strcpy(s + i, bot->bot);
    i += strlen(bot->bot);
  }
  if (s[0])
    dprintf(idx, STR("Bots: %s\n"), s);
  dprintf(idx, STR("(Total up: %d)\n"), tands + 1);

}

/* Show a simpleton bot tree
 */
void tell_bottree(int idx, int showver)
{
  char s[161];
  tand_t *last[20], *this, *bot, *bot2 = NULL;
  int lev = 0, more = 1, mark[20], ok, cnt, i, imark;
  char work[1024];
  int tothops = 0;

  if (tands == 0) {
    dprintf(idx, "%s\n", BOT_NOBOTSLINKED);
    return;
  }
  s[0] = 0;
  i = 0;

  for (bot = tandbot; bot; bot = bot->next)
    if (!bot->uplink) {
      if (i) {
	s[i++] = ',';
	s[i++] = ' ';
      }
      strcpy(s + i, bot->bot);
      i += strlen(bot->bot);
    }
  if (s[0])
    dprintf(idx, "(%s %s)\n", BOT_NOTRACEINFO, s);
  if (showver)
    dprintf(idx, "%s (%lu)\n", conf.bot->nick, buildts);
  else
    dprintf(idx, "%s\n", conf.bot->nick);
  this = (tand_t *) 1;
  work[0] = 0;
  while (more) {
    if (lev == 20) {
      dprintf(idx, "\n%s\n", BOT_COMPLEXTREE);
      return;
    }
    cnt = 0;
    tothops += lev;
    for (bot = tandbot; bot; bot = bot->next)
      if (bot->uplink == this)
	cnt++;
    if (cnt) {
      imark = 0;
      for (i = 0; i < lev; i++) {
	if (mark[i])
	  strcpy(work + imark, "  |  ");
	else
	  strcpy(work + imark, "     ");
	imark += 5;
      }
      if (cnt > 1)
	strcpy(work + imark, "  |-");
      else
	strcpy(work + imark, "  `-");
      s[0] = 0;
      bot = tandbot;
      while (!s[0]) {
	if (bot->uplink == this) {
	  if (bot->ver) {
	    i = sprintf(s, "%c%s", bot->share, bot->bot);
	    if (showver)
	      sprintf(s + i, " (%d.%d.%d.%d)",
		      bot->ver / 1000000,
		      bot->ver % 1000000 / 10000,
		      bot->ver % 10000 / 100,
		      bot->ver % 100);
	  } else {
	    sprintf(s, "-%s", bot->bot);
	  }
	} else
	  bot = bot->next;
      }
      dprintf(idx, "%s%s\n", work, s);
      if (cnt > 1)
	mark[lev] = 1;
      else
	mark[lev] = 0;
      work[0] = 0;
      last[lev] = this;
      this = bot;
      lev++;
      more = 1;
    } else {
      while (cnt == 0) {
	/* No subtrees from here */
	if (lev == 0) {
	  dprintf(idx, "(( tree error ))\n");
	  return;
	}
	ok = 0;
	for (bot = tandbot; bot; bot = bot->next) {
	  if (bot->uplink == last[lev - 1]) {
	    if (this == bot)
	      ok = 1;
	    else if (ok) {
	      cnt++;
	      if (cnt == 1) {
		bot2 = bot;
		if (bot->ver) {
		  i = sprintf(s, "%c%s", bot->share, bot->bot);
		  if (showver)
		    sprintf(s + i, " (%d.%d.%d.%d)",
			    bot->ver / 1000000,
			    bot->ver % 1000000 / 10000,
			    bot->ver % 10000 / 100,
			    bot->ver % 100);
		} else {
		  sprintf(s, "-%s", bot->bot);
		}
	      }
	    }
	  }
	}
	if (cnt) {
	  imark = 0;
	  for (i = 1; i < lev; i++) {
	    if (mark[i - 1])
	      strcpy(work + imark, "  |  ");
	    else
	      strcpy(work + imark, "     ");
	    imark += 5;
	  }
	  more = 1;
	  if (cnt > 1)
	    dprintf(idx, "%s  |-%s\n", work, s);
	  else
	    dprintf(idx, "%s  `-%s\n", work, s);
	  this = bot2;
	  work[0] = 0;
	  if (cnt > 1)
	    mark[lev - 1] = 1;
	  else
	    mark[lev - 1] = 0;
	} else {
	  /* This was the last child */
	  lev--;
	  if (lev == 0) {
	    more = 0;
	    cnt = 999;
	  } else {
	    more = 1;
	    this = last[lev];
	  }
	}
      }
    }
  }
  /* Hop information: (9d) */
  dprintf(idx, "Average hops: %3.1f, total bots: %d\n",
	  ((float) tothops) / ((float) tands), tands + 1);
}

/* Dump list of links to a new bot
 */
void dump_links(int z)
{
  register int i, l;
  char x[1024];
  tand_t *bot;

  for (bot = tandbot; bot; bot = bot->next) {
    char *p;

    if (bot->uplink == (tand_t *) 1)
      p = conf.bot->nick;
    else
      p = bot->uplink->bot;
    l = simple_sprintf(x, "n %s %s %c%D\n", bot->bot, p,
			 bot->share, bot->ver);
    tputs(dcc[z].sock, x, l);
  }
  if (!(bot_flags(dcc[z].user) & BOT_ISOLATE)) {
    /* Dump party line members */
    for (i = 0; i < dcc_total; i++) {
      if (dcc[i].type == &DCC_CHAT) {
	if ((dcc[i].u.chat->channel >= 0) &&
	    (dcc[i].u.chat->channel < GLOBAL_CHANS)) {
          l = simple_sprintf(x, "j !%s %s %D %c%D %s\n",
			       conf.bot->nick, dcc[i].nick,
			       dcc[i].u.chat->channel, geticon(i),
			       dcc[i].sock, dcc[i].host);
	  tputs(dcc[z].sock, x, l);
          l = simple_sprintf(x, "i %s %D %D %s\n", conf.bot->nick,
			       dcc[i].sock, now - dcc[i].timeval,
			 dcc[i].u.chat->away ? dcc[i].u.chat->away : "");
	  tputs(dcc[z].sock, x, l);
	}
      }
    }
    for (i = 0; i < parties; i++) {
      l = simple_sprintf(x, "j %s %s %D %c%D %s\n",
			   party[i].bot, party[i].nick,
			   party[i].chan, party[i].flag,
			   party[i].sock, party[i].from);
      tputs(dcc[z].sock, x, l);
      if ((party[i].status & PLSTAT_AWAY) || (party[i].timer != 0)) {
        l = simple_sprintf(x, "i %s %D %D %s\n", party[i].bot,
			     party[i].sock, now - party[i].timer,
			     party[i].away ? party[i].away : "");
	tputs(dcc[z].sock, x, l);
      }
    }
  }
}

int in_chain(char *who)
{
  if (findbot(who))
    return 1;
  if (!egg_strcasecmp(who, conf.bot->nick))
    return 1;
  return 0;
}

int bots_in_subtree(tand_t *bot)
{
  int nr = 1;
  tand_t *b;

  if (!bot)
    return 0;
  for (b = tandbot; b; b = b->next) {
    if (b->bot && (b->uplink == bot)) {
      nr += bots_in_subtree(b);
    }
  }
  return nr;
}

int users_in_subtree(tand_t *bot)
{
  int i, nr;
  tand_t *b;

  nr = 0;
  if (!bot)
    return 0;
  for (i = 0; i < parties; i++)
    if (!egg_strcasecmp(party[i].bot, bot->bot))
      nr++;
  for (b = tandbot; b; b = b->next)
    if (b->bot && (b->uplink == bot))
      nr += users_in_subtree(b);
  return nr;
}

/* Break link with a tandembot
 */
int botunlink(int idx, char *nick, char *reason)
{
  char s[20];
  register int i;
  int bots, users;
  tand_t *bot;

  if (nick[0] == '*')
    dprintf(idx, "%s\n", BOT_UNLINKALL);
  for (i = 0; i < dcc_total; i++) {
    if ((nick[0] == '*') || !egg_strcasecmp(dcc[i].nick, nick)) {
      if (dcc[i].type == &DCC_FORK_BOT) {
	if (idx >= 0)
	  dprintf(idx, "%s: %s -> %s.\n", BOT_KILLLINKATTEMPT,
		  dcc[i].nick, dcc[i].host);
	putlog(LOG_BOTS, "*", "%s: %s -> %s:%d",
	       BOT_KILLLINKATTEMPT, dcc[i].nick,
	       dcc[i].host, dcc[i].port);
	killsock(dcc[i].sock);
	lostdcc(i);
	if (nick[0] != '*')
	  return 1;
      } else if (dcc[i].type == &DCC_BOT_NEW) {
	if (idx >= 0)
	  dprintf(idx, "%s %s.\n", BOT_ENDLINKATTEMPT,
		  dcc[i].nick);
	putlog(LOG_BOTS, "*", "%s %s @ %s:%d",
	       "Stopped trying to link", dcc[i].nick,
	       dcc[i].host, dcc[i].port);
	killsock(dcc[i].sock);
	lostdcc(i);
	if (nick[0] != '*')
	  return 1;
      } else if (dcc[i].type == &DCC_BOT) {
	char s[1024];

	if (idx >= 0)
	  dprintf(idx, "%s %s.\n", BOT_BREAKLINK, dcc[i].nick);
	else if ((idx == -3) && (b_status(i) & STAT_SHARE) && !share_unlinks)
	  return -1;
	bot = findbot(dcc[i].nick);
	bots = bots_in_subtree(bot);
	users = users_in_subtree(bot);
	if (reason && reason[0]) {
	  simple_sprintf(s, "%s %s (%s) (lost %d bot%s and %d user%s)",
	  		 BOT_UNLINKEDFROM, dcc[i].nick, reason, bots,
			 (bots != 1) ? "s" : "", users, (users != 1) ?
			 "s" : "");
	  dprintf(i, "bye %s\n", reason);
	} else {
	  simple_sprintf(s, "%s %s (lost %d bot%s and %d user%s)",
	  		 BOT_UNLINKEDFROM, dcc[i].nick, bots, (bots != 1) ?
			 "s" : "", users, (users != 1) ? "s" : "");
	  dprintf(i, "bye No reason\n");
	}
	chatout("*** %s\n", s);
	botnet_send_unlinked(i, dcc[i].nick, s);
	killsock(dcc[i].sock);
	lostdcc(i);
	if (nick[0] != '*')
	  return 1;
      }
    }
  }
  if (idx >= 0 && nick[0] != '*')
    dprintf(idx, "%s\n", BOT_NOTCONNECTED);
  if (nick[0] != '*') {
    bot = findbot(nick);
    if (bot) {
      /* The internal bot list is desynched from the dcc list
         sometimes. While we still search for the bug, provide
         an easy way to clear out those `ghost'-bots.
				       Fabian (2000-08-02)  */
      char *ghost = "BUG!!: Found bot `%s' in internal bot list, but it\n"
		    "   shouldn't have been there! Removing.\n"
		    "   This is a known bug we haven't fixed yet. If this\n"
		    "   bot is the newest eggdrop version available and you\n"
		    "   know a *reliable* way to reproduce the bug, please\n"
		    "   contact us - we need your help!\n";
      if (idx >= 0)
	dprintf(idx, ghost, nick);
      else
	putlog(LOG_MISC, "*", ghost, nick);
      rembot(bot->bot);
      return 1;
    }
  }
  if (nick[0] == '*') {
    dprintf(idx, "%s\n", BOT_WIPEBOTTABLE);
    while (tandbot)
      rembot(tandbot->bot);
    while (parties) {
      parties--;
      /* Assert? */
      if (party[i].chan >= 0)
        check_bind_chpt(party[i].bot, party[i].nick, party[i].sock,
		       party[i].chan);
    }
    strcpy(s, "killassoc &");
    Tcl_Eval(interp, s);
  }
  return 0;
}

static void botlink_resolve_success(int);
static void botlink_resolve_failure(int);

/* Link to another bot
 */
int botlink(char *linker, int idx, char *nick)
{
  struct bot_addr *bi;
  struct userrec *u;
  register int i;

  u = get_user_by_handle(userlist, nick);
  if (!u || !(u->flags & USER_BOT)) {
    if (idx >= 0)
      dprintf(idx, "%s %s\n", nick, BOT_BOTUNKNOWN);
  } else if (!egg_strcasecmp(nick, conf.bot->nick)) {
    if (idx >= 0)
      dprintf(idx, "%s\n", BOT_CANTLINKMYSELF);
  } else if (in_chain(nick) && (idx != -3)) {
    if (idx >= 0)
      dprintf(idx, "%s\n", BOT_ALREADYLINKED);
  } else {
    for (i = 0; i < dcc_total; i++)
      if ((dcc[i].user == u) &&
	  ((dcc[i].type == &DCC_FORK_BOT) ||
	   (dcc[i].type == &DCC_BOT_NEW))) {
	if (idx >= 0)
	  dprintf(idx, "%s\n", BOT_ALREADYLINKING);
	return 0;
      }
    /* Address to connect to is in 'info' */
    bi = (struct bot_addr *) get_user(&USERENTRY_BOTADDR, u);
    if (!bi || !strlen(bi->address) || !bi->telnet_port || (bi->telnet_port <= 0)) {
      if (idx >= 0) {
	dprintf(idx, "%s '%s'.\n", BOT_NOTELNETADDY, nick);
	dprintf(idx, "%s .chaddr %s %s\n",
		MISC_USEFORMAT, nick, MISC_CHADDRFORMAT);
      }
    } else if (dcc_total == max_dcc) {
      if (idx >= 0)
	dprintf(idx, "%s\n", DCC_TOOMANYDCCS1);
    } else {
      correct_handle(nick);

      if (idx > -2)
	putlog(LOG_BOTS, "*", "%s %s at %s:%d ...", BOT_LINKING, nick,
	       bi->address, bi->telnet_port);

      i = new_dcc(&DCC_DNSWAIT, sizeof(struct dns_info));
      dcc[i].timeval = now;
      dcc[i].port = bi->telnet_port;
      dcc[i].user = u;
      strcpy(dcc[i].nick, nick);
      strcpy(dcc[i].host, bi->address);
      dcc[i].u.dns->ibuf = idx;
      dcc[i].u.dns->cptr = calloc(1, strlen(linker) + 1);
      strcpy(dcc[i].u.dns->cptr, linker);
      dcc[i].u.dns->host = calloc(1, strlen(dcc[i].host) + 1);
      strcpy(dcc[i].u.dns->host, dcc[i].host);
      dcc[i].u.dns->dns_success = botlink_resolve_success;
      dcc[i].u.dns->dns_failure = botlink_resolve_failure;
      dcc[i].u.dns->dns_type = RES_IPBYHOST;
      dcc[i].u.dns->type = &DCC_FORK_BOT;
#ifdef USE_IPV6
      botlink_resolve_success(i);
#else
      dcc_dnsipbyhost(bi->address);
#endif /* USE_IPV6 */
      return 1;
    }
  }
  return 0;
}

static void botlink_resolve_failure(int i)
{
  free(dcc[i].u.dns->cptr);
  lostdcc(i);
}

static void botlink_resolve_success(int i)
{
  int idx = dcc[i].u.dns->ibuf;
  char *linker = dcc[i].u.dns->cptr;

  dcc[i].addr = dcc[i].u.dns->ip;
  changeover_dcc(i, &DCC_FORK_BOT, sizeof(struct bot_info));
  dcc[i].timeval = now;
  strcpy(dcc[i].u.bot->linker, linker);
  strcpy(dcc[i].u.bot->version, "(primitive bot)");
  strcpy(dcc[i].u.bot->sysname, "*");
  dcc[i].u.bot->numver = idx;
  dcc[i].u.bot->port = dcc[i].port;		/* Remember where i started */
#ifdef USE_IPV6
  dcc[i].sock = getsock(SOCK_STRONGCONN, hostprotocol(dcc[i].host));
#else
  dcc[i].sock = getsock(SOCK_STRONGCONN);
#endif /* USE_IPV6 */
  free(linker);

  if (dcc[i].sock < 0 ||
#ifdef USE_IPV6
      open_telnet_raw(dcc[i].sock, dcc[i].host,
#else
      open_telnet_raw(dcc[i].sock, iptostr(htonl(dcc[i].addr)),
#endif /* USE_IPV6 */
		      dcc[i].port) < 0)
    failed_link(i);
  else { /* let's attempt to initiate SSL before ANYTHING else... */
#ifdef HAVE_SSL
/*    if (!ssl_link(dcc[i].sock, CONNECT_SSL)) {
      debug2("back from ssl_link(%d, %d) for botlink (failed)", dcc[i].sock, CONNECT_SSL);
      dcc[i].ssl = 0;
      putlog(LOG_BOTS, "*", "SSL link for '%s' failed", dcc[i].nick); 
    } else */
      dcc[i].ssl = 1;
#else
      dcc[i].ssl = 0;
#endif /* HAVE_SSL */
  }
}

static void failed_tandem_relay(int idx)
{
  int uidx = (-1), i;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_PRE_RELAY) &&
	(dcc[i].u.relay->sock == dcc[idx].sock))
      uidx = i;
  if (uidx < 0) {
    putlog(LOG_MISC, "*", "%s  %d -> %d", BOT_CANTFINDRELAYUSER,
	   dcc[idx].sock, dcc[idx].u.relay->sock);
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  if (dcc[idx].port >= dcc[idx].u.relay->port + 3) {
    struct chat_info *ci = dcc[uidx].u.relay->chat;

    dprintf(uidx, "%s %s.\n", BOT_CANTLINKTO, dcc[idx].nick);
    dcc[uidx].status = dcc[uidx].u.relay->old_status;
    free(dcc[uidx].u.relay);
    dcc[uidx].u.chat = ci;
    dcc[uidx].type = &DCC_CHAT;
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  killsock(dcc[idx].sock);
#ifdef USE_IPV6
  dcc[idx].sock = getsock(SOCK_STRONGCONN, hostprotocol(dcc[idx].host));
#else
  dcc[idx].sock = getsock(SOCK_STRONGCONN);
#endif /* USE_IPV6 */
  dcc[uidx].u.relay->sock = dcc[idx].sock;
  dcc[idx].port++;
  dcc[idx].timeval = now;
  if (dcc[idx].sock < 0 ||
#ifdef USE_IPV6
      open_telnet_raw(dcc[idx].sock, dcc[idx].host,
#else
  if (dcc[idx].sock < 0 ||
      open_telnet_raw(dcc[idx].sock, dcc[idx].addr ?
				     iptostr(htonl(dcc[idx].addr)) :
				     dcc[idx].host, 
#endif /* USE_IPV6 */
      dcc[idx].port) < 0)
    failed_tandem_relay(idx);
}


static void tandem_relay_resolve_failure(int);
static void tandem_relay_resolve_success(int);

/* Relay to another tandembot
 */
void tandem_relay(int idx, char *nick, register int i)
{
  struct userrec *u;
  struct bot_addr *bi;
  struct chat_info *ci;

  u = get_user_by_handle(userlist, nick);
  if (!u || !(u->flags & USER_BOT)) {
    dprintf(idx, "%s %s\n", nick, BOT_BOTUNKNOWN);
    return;
  }
  if (!egg_strcasecmp(nick, conf.bot->nick)) {
    dprintf(idx, "%s\n", BOT_CANTRELAYMYSELF);
    return;
  }
  /* Address to connect to is in 'info' */
  bi = (struct bot_addr *) get_user(&USERENTRY_BOTADDR, u);
  if (!bi || !strlen(bi->address) || !bi->relay_port || (bi->relay_port <= 0)) {
    dprintf(idx, "%s '%s'.\n", BOT_NOTELNETADDY, nick);
    dprintf(idx, "%s .chaddr %s %s\n",
	    MISC_USEFORMAT, nick, MISC_CHADDRFORMAT);

    return;
  }
  i = new_dcc(&DCC_DNSWAIT, sizeof(struct dns_info));
  if (i < 0) {
    dprintf(idx, "%s\n", DCC_TOOMANYDCCS1);
    return;
  }

#ifdef USE_IPV6
  dcc[i].sock = getsock(SOCK_STRONGCONN | SOCK_VIRTUAL, hostprotocol(bi->address));
#else
  dcc[i].sock = getsock(SOCK_STRONGCONN | SOCK_VIRTUAL);
#endif /* USE_IPV6 */

  if (dcc[i].sock < 0) {
    lostdcc(i);
    dprintf(idx, "%s\n", MISC_NOFREESOCK);
    return;
  }

  dcc[i].port = bi->relay_port;
  dcc[i].addr = 0L;
  strcpy(dcc[i].nick, nick);
  dcc[i].user = u;
  strcpy(dcc[i].host, bi->address);
#ifdef HUB
  dprintf(idx, "%s %s @ %s:%d ...\n", BOT_CONNECTINGTO, nick, bi->address, bi->relay_port);
#endif /* HUB */
  dprintf(idx, "%s\n", BOT_BYEINFO1);
  dcc[idx].type = &DCC_PRE_RELAY;
  ci = dcc[idx].u.chat;
  dcc[idx].u.relay = calloc(1, sizeof(struct relay_info));
  dcc[idx].u.relay->chat = ci;
  dcc[idx].u.relay->old_status = dcc[idx].status;
  dcc[idx].u.relay->sock = dcc[i].sock;
  dcc[i].timeval = now;
  dcc[i].u.dns->ibuf = dcc[idx].sock;
  dcc[i].u.dns->host = calloc(1, strlen(bi->address) + 1);
  strcpy(dcc[i].u.dns->host, bi->address);
  dcc[i].u.dns->dns_success = tandem_relay_resolve_success;
  dcc[i].u.dns->dns_failure = tandem_relay_resolve_failure;
  dcc[i].u.dns->dns_type = RES_IPBYHOST;
  dcc[i].u.dns->type = &DCC_FORK_RELAY;
#ifdef USE_IPV6
  tandem_relay_resolve_success(i);
#else
  dcc_dnsipbyhost(bi->address);
#endif /* USE_IPV6 */

}

static void tandem_relay_resolve_failure(int idx)
{
  struct chat_info *ci;
  register int uidx = (-1), i;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_PRE_RELAY) &&
	(dcc[i].u.relay->sock == dcc[idx].sock)) {
      uidx = i;
      break;
    }
  if (uidx < 0) {
    putlog(LOG_MISC, "*", "%s  %d -> %d", BOT_CANTFINDRELAYUSER,
	   dcc[idx].sock, dcc[idx].u.relay->sock);
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  ci = dcc[uidx].u.relay->chat;
  dprintf(uidx, "%s %s.\n", BOT_CANTLINKTO, dcc[idx].nick);
  dcc[uidx].status = dcc[uidx].u.relay->old_status;
  free(dcc[uidx].u.relay);
  dcc[uidx].u.chat = ci;
  dcc[uidx].type = &DCC_CHAT;
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void tandem_relay_resolve_success(int i)
{
  int sock = dcc[i].u.dns->ibuf;

  dcc[i].addr = dcc[i].u.dns->ip;
  changeover_dcc(i, &DCC_FORK_RELAY, sizeof(struct relay_info));
  dcc[i].u.relay->chat = calloc(1, sizeof(struct chat_info));

  dcc[i].u.relay->sock = sock;
  dcc[i].u.relay->port = dcc[i].port;
  dcc[i].u.relay->chat->away = NULL;
  dcc[i].u.relay->chat->msgs_per_sec = 0;
  dcc[i].u.relay->chat->con_flags = 0;
  dcc[i].u.relay->chat->buffer = NULL;
  dcc[i].u.relay->chat->max_line = 0;
  dcc[i].u.relay->chat->line_count = 0;
  dcc[i].u.relay->chat->current_lines = 0;
  dcc[i].timeval = now;
#ifdef USE_IPV6
  if (open_telnet_raw(dcc[i].sock, dcc[i].host,
#else
  if (open_telnet_raw(dcc[i].sock, iptostr(htonl(dcc[i].addr)),
#endif /* USE_IPV6 */
		      dcc[i].port) < 0)
    failed_tandem_relay(i);
}

/* Input from user before connect is ready
 */
static void pre_relay(int idx, char *buf, register int i)
{
  register int tidx = (-1);

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_FORK_RELAY) &&
	(dcc[i].u.relay->sock == dcc[idx].sock)) {
      tidx = i;
      break;
    }
  if (tidx < 0) {
    /* Now try to find it among the DNSWAIT sockets instead. */
    for (i = 0; i < dcc_total; i++)
      if ((dcc[i].type == &DCC_DNSWAIT) &&
	  (dcc[i].sock == dcc[idx].u.relay->sock)) {
	tidx = i;
	break;
      }
  }
  if (tidx < 0) {
    putlog(LOG_MISC, "*", "%s  %d -> %d", BOT_CANTFINDRELAYUSER,
	   dcc[idx].sock, dcc[idx].u.relay->sock);
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  if (!egg_strcasecmp(buf, "*bye*")) {
    /* Disconnect */
    struct chat_info *ci = dcc[idx].u.relay->chat;

    dprintf(idx, "%s %s.\n", BOT_ABORTRELAY1, dcc[tidx].nick);
    dprintf(idx, "%s %s.\n\n", BOT_ABORTRELAY2, conf.bot->nick);
    putlog(LOG_MISC, "*", "%s %s -> %s", BOT_ABORTRELAY3, dcc[idx].nick,
	   dcc[tidx].nick);
    dcc[idx].status = dcc[idx].u.relay->old_status;
    free(dcc[idx].u.relay);
    dcc[idx].u.chat = ci;
    dcc[idx].type = &DCC_CHAT;
    killsock(dcc[tidx].sock);
    lostdcc(tidx);
    return;
  }
}

/* User disconnected before her relay had finished connecting
 */
static void failed_pre_relay(int idx)
{
  register int tidx = (-1), i;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_FORK_RELAY) &&
	(dcc[i].u.relay->sock == dcc[idx].sock)) {
      tidx = i;
      break;
    }
  if (tidx < 0) {
    /* Now try to find it among the DNSWAIT sockets instead. */
    for (i = 0; i < dcc_total; i++)
      if ((dcc[i].type == &DCC_DNSWAIT) &&
	  (dcc[i].sock == dcc[idx].u.relay->sock)) {
	tidx = i;
	break;
      }
  }
  if (tidx < 0) {
    putlog(LOG_MISC, "*", "%s  %d -> %d", BOT_CANTFINDRELAYUSER,
	   dcc[idx].sock, dcc[idx].u.relay->sock);
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  putlog(LOG_MISC, "*", "%s [%s]%s/%d", BOT_LOSTDCCUSER, dcc[idx].nick,
	 dcc[idx].host, dcc[idx].port);
  putlog(LOG_MISC, "*", "(%s %s)", BOT_DROPPINGRELAY, dcc[tidx].nick);
  if ((dcc[tidx].sock != STDOUT) || backgrd) {
    if (idx > tidx) {
      int t = tidx;

      tidx = idx;
      idx = t;
    }
    killsock(dcc[tidx].sock);
    lostdcc(tidx);
  } else {
    fatal("Lost my terminal?!", 0);
  }
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void cont_tandem_relay(int idx, char *buf, register int i)
{
  register int uidx = (-1);
  struct relay_info *ri;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_PRE_RELAY) &&
	(dcc[i].u.relay->sock == dcc[idx].sock))
      uidx = i;
  if (uidx < 0) {
    putlog(LOG_MISC, "*", "%s  %d -> %d", BOT_CANTFINDRELAYUSER,
	   dcc[i].sock, dcc[i].u.relay->sock);
    killsock(dcc[i].sock);
    lostdcc(i);
    return;
  }
  dcc[idx].type = &DCC_RELAY;
  dcc[idx].u.relay->sock = dcc[uidx].sock;
  dcc[uidx].u.relay->sock = dcc[idx].sock;
  dprintf(uidx, "%s %s ...\n", BOT_RELAYSUCCESS, dcc[idx].nick);
  dprintf(uidx, "%s\n\n", BOT_BYEINFO2);
  putlog(LOG_MISC, "*", "%s %s -> %s", BOT_RELAYLINK,
	 dcc[uidx].nick, dcc[idx].nick);
  ri = dcc[uidx].u.relay;	/* YEAH */
  dcc[uidx].type = &DCC_CHAT;
  dcc[uidx].u.chat = ri->chat;
  if (dcc[uidx].u.chat->channel >= 0) {
    chanout_but(-1, dcc[uidx].u.chat->channel, "*** %s %s\n",
		dcc[uidx].nick, BOT_PARTYLEFT);
    if (dcc[uidx].u.chat->channel < GLOBAL_CHANS)
      botnet_send_part_idx(uidx, NULL);
    check_bind_chpt(conf.bot->nick, dcc[uidx].nick, dcc[uidx].sock,
		   dcc[uidx].u.chat->channel);
  }
  check_bind_chof(dcc[uidx].nick, uidx);
  dcc[uidx].type = &DCC_RELAYING;
  dcc[uidx].u.relay = ri;
}

static void eof_dcc_relay(int idx)
{
  register int j;
  struct chat_info *ci;

  for (j = 0; j < dcc_total; j++)
    if (dcc[j].sock == dcc[idx].u.relay->sock)
      break;
  if (j == dcc_total) {
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  dcc[j].status = dcc[j].u.relay->old_status;
  /* In case echo was off, turn it back on (send IAC WON'T ECHO): */
  if (dcc[j].status & STAT_TELNET)
    dprintf(j, TLN_IAC_C TLN_WONT_C TLN_ECHO_C "\n");
  putlog(LOG_MISC, "*", "%s: %s -> %s", BOT_ENDRELAY1, dcc[j].nick,
	 dcc[idx].nick);
  dprintf(j, "\n\n*** %s %s\n", BOT_ENDRELAY2, conf.bot->nick);
  ci = dcc[j].u.relay->chat;
  free(dcc[j].u.relay);
  dcc[j].u.chat = ci;
  dcc[j].type = &DCC_CHAT;
  if (dcc[j].u.chat->channel >= 0) {
    chanout_but(-1, dcc[j].u.chat->channel, "*** %s %s.\n",
		dcc[j].nick, BOT_PARTYREJOINED);
    if (dcc[j].u.chat->channel < GLOBAL_CHANS)
      botnet_send_join_idx(j, -1);
  }
  check_bind_chon(dcc[j].nick, j);
  check_bind_chjn(conf.bot->nick, dcc[j].nick, dcc[j].u.chat->channel,
		 geticon(j), dcc[j].sock, dcc[j].host);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void eof_dcc_relaying(int idx)
{
  register int j, x = dcc[idx].u.relay->sock;

  putlog(LOG_MISC, "*", "%s [%s]%s/%d", BOT_LOSTDCCUSER, dcc[idx].nick,
	 dcc[idx].host, dcc[idx].port);
  killsock(dcc[idx].sock);
  lostdcc(idx);
  for (j = 0; (dcc[j].sock != x) || (dcc[j].type == &DCC_FORK_RELAY); j++);
  putlog(LOG_MISC, "*", "(%s %s)", BOT_DROPPEDRELAY, dcc[j].nick);
  killsock(dcc[j].sock);
  lostdcc(j);			/* Drop connection to the bot */
}

static void dcc_relay(int idx, char *buf, int j)
{
  unsigned char *p = (unsigned char *) buf;
  int mark;

  for (j = 0; dcc[j].sock != dcc[idx].u.relay->sock ||
       dcc[j].type != &DCC_RELAYING; j++);
  /* If redirecting to a non-telnet user, swallow telnet codes and
     escape sequences. */
  if (!(dcc[j].status & STAT_TELNET)) {
    while (*p != 0) {
      while (*p != 255 && (*p != '\033' || *(p + 1) != '[') && *p != '\r' && *p)
	p++;			/* Search for IAC, escape sequences and CR. */
      if (*p == 255) {
	mark = 2;
	if (!*(p + 1))
	  mark = 1;		/* Bogus */
	if ((*(p + 1) >= 251) || (*(p + 1) <= 254)) {
	  mark = 3;
	  if (!*(p + 2))
	    mark = 2;		/* Bogus */
	}
	strcpy((char *) p, (char *) (p + mark));
      } else if (*p == '\033') {
	unsigned char	*e;

	// Search for the end of the escape sequence.
	for (e = p + 2; *e != 'm' && *e; e++)
	  ;
	strcpy((char *) p, (char *) (e + 1));
      } else if (*p == '\r')
	strcpy((char *) p, (char *) (p + 1));
    }
    if (!buf[0])
      dprintf(-dcc[idx].u.relay->sock, " \n");
    else
      dprintf(-dcc[idx].u.relay->sock, "%s\n", buf);
    return;
  }
  /* Telnet user */
  if (!buf[0])
    dprintf(-dcc[idx].u.relay->sock, " \r\n");
  else
    dprintf(-dcc[idx].u.relay->sock, "%s\r\n", buf);
}

static void dcc_relaying(int idx, char *buf, int j)
{
  struct chat_info *ci;

  if (egg_strcasecmp(buf, "*bye*")) {
    dprintf(-dcc[idx].u.relay->sock, "%s\n", buf);
    return;
  }
  for (j = 0; (dcc[j].sock != dcc[idx].u.relay->sock) ||
       (dcc[j].type != &DCC_RELAY); j++);
  dcc[idx].status = dcc[idx].u.relay->old_status;
  /* In case echo was off, turn it back on (send IAC WON'T ECHO): */
  if (dcc[idx].status & STAT_TELNET)
    dprintf(idx, TLN_IAC_C TLN_WONT_C TLN_ECHO_C "\n");
  dprintf(idx, "\n(%s %s.)\n", BOT_BREAKRELAY, dcc[j].nick);
  dprintf(idx, "%s %s.\n\n", BOT_ABORTRELAY2, conf.bot->nick);
  putlog(LOG_MISC, "*", "%s: %s -> %s", BOT_RELAYBROKEN,
	 dcc[idx].nick, dcc[j].nick);
  if (dcc[idx].u.relay->chat->channel >= 0) {
    chanout_but(-1, dcc[idx].u.relay->chat->channel,
		"*** %s joined the party line.\n", dcc[idx].nick);
    if (dcc[idx].u.relay->chat->channel < GLOBAL_CHANS)
      botnet_send_join_idx(idx, -1);
  }
  ci = dcc[idx].u.relay->chat;
  free(dcc[idx].u.relay);
  dcc[idx].u.chat = ci;
  dcc[idx].type = &DCC_CHAT;
  check_bind_chon(dcc[idx].nick, idx);
  if (dcc[idx].u.chat->channel >= 0)
    check_bind_chjn(conf.bot->nick, dcc[idx].nick, dcc[idx].u.chat->channel,
		   geticon(idx), dcc[idx].sock, dcc[idx].host);
  killsock(dcc[j].sock);
  lostdcc(j);
}

static void display_relay(int i, char *other)
{
  sprintf(other, "rela  -> sock %d", dcc[i].u.relay->sock);
}

static void display_relaying(int i, char *other)
{
  sprintf(other, ">rly  -> sock %d", dcc[i].u.relay->sock);
}

static void display_tandem_relay(int i, char *other)
{
  strcpy(other, "other  rela");
}

static void display_pre_relay(int i, char *other)
{
  strcpy(other, "other  >rly");
}

static void kill_relay(int idx, void *x)
{
  register struct relay_info *p = (struct relay_info *) x;

  if (p->chat)
    DCC_CHAT.kill(idx, p->chat);
  free(p);
}

struct dcc_table DCC_RELAY =
{
  "RELAY",
  0,				/* Flags */
  eof_dcc_relay,
  dcc_relay,
  NULL,
  NULL,
  display_relay,
  kill_relay,
  NULL
};

static void out_relay(int idx, char *buf, void *x)
{
  register struct relay_info *p = (struct relay_info *) x;

  if (p && p->chat)
    DCC_CHAT.output(idx, buf, p->chat);
  else
    tputs(dcc[idx].sock, buf, strlen(buf));
}

struct dcc_table DCC_RELAYING =
{
  "RELAYING",
  0,				/* Flags */
  eof_dcc_relaying,
  dcc_relaying,
  NULL,
  NULL,
  display_relaying,
  kill_relay,
  out_relay
};

struct dcc_table DCC_FORK_RELAY =
{
  "FORK_RELAY",
  0,				/* Flags */
  failed_tandem_relay,
  cont_tandem_relay,
  &connect_timeout,
  failed_tandem_relay,
  display_tandem_relay,
  kill_relay,
  NULL
};

struct dcc_table DCC_PRE_RELAY =
{
  "PRE_RELAY",
  0,				/* Flags */
  failed_pre_relay,
  pre_relay,
  NULL,
  NULL,
  display_pre_relay,
  kill_relay,
  NULL
};

/* Once a minute, send 'ping' to each bot -- no exceptions
 */
void check_botnet_pings()
{
  int i;
  int bots, users;
  tand_t *bot;

  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type == &DCC_BOT)
      if (dcc[i].status & STAT_PINGED) {
	char s[1024];

	putlog(LOG_BOTS, "*", "%s: %s", BOT_PINGTIMEOUT, dcc[i].nick);
	bot = findbot(dcc[i].nick);
	bots = bots_in_subtree(bot);
	users = users_in_subtree(bot);
	simple_sprintf(s, "%s: %s (lost %d bot%s and %d user%s)", BOT_PINGTIMEOUT,
		       dcc[i].nick, bots, (bots != 1) ? "s" : "",
		       users, (users != 1) ? "s" : "");
	chatout("*** %s\n", s);
	botnet_send_unlinked(i, dcc[i].nick, s);
	killsock(dcc[i].sock);
	lostdcc(i);
      }
  }
  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type == &DCC_BOT) {
      botnet_send_ping(i);
      dcc[i].status |= STAT_PINGED;
    }
  }
  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type == &DCC_BOT) && (dcc[i].status & STAT_LEAF)) {
      tand_t *bot, *via = findbot(dcc[i].nick);

      for (bot = tandbot; bot; bot = bot->next) {
	if ((via == bot->via) && (bot != via)) {
	  /* Not leaflike behavior */
	  if (dcc[i].status & STAT_WARNED) {
	    char s[1024];

	    putlog(LOG_BOTS, "*", "%s %s (%s).", BOT_DISCONNECTED,
		   dcc[i].nick, BOT_BOTNOTLEAFLIKE);
	    dprintf(i, "bye %s\n", BOT_BOTNOTLEAFLIKE);
	    bot = findbot(dcc[i].nick);
	    bots = bots_in_subtree(bot);
	    users = users_in_subtree(bot);
	    simple_sprintf(s, "%s %s (%s) (lost %d bot%s and %d user%s)",
	    		   BOT_DISCONNECTED, dcc[i].nick, BOT_BOTNOTLEAFLIKE,
			   bots, (bots != 1) ? "s" : "", users, (users != 1) ?
			   "s" : "");
	    chatout("*** %s\n", s);
	    botnet_send_unlinked(i, dcc[i].nick, s);
	    killsock(dcc[i].sock);
	    lostdcc(i);
	  } else {
            putlog(LOG_MISC, "*", "I am lame, and am now rejecting %s", bot->bot);
	    botnet_send_reject(i, conf.bot->nick, NULL, bot->bot,
			       NULL, NULL);
	    dcc[i].status |= STAT_WARNED;
	  }
	} else
	  dcc[i].status &= ~STAT_WARNED;
      }
    }
  }
#ifdef HUB
#ifdef S_AUTOLOCK
  local_check_should_lock();
#endif /* S_AUTOLOCK */
#ifdef G_BACKUP
  check_should_backup();
#endif /* G_BACKUP */
#endif /* HUB */
}

void zapfbot(int idx)
{
  char s[1024];
  int bots, users;
  tand_t *bot;

  bot = findbot(dcc[idx].nick);
  bots = bots_in_subtree(bot);
  users = users_in_subtree(bot);
  simple_sprintf(s, "%s: %s (lost %d bot%s and %d user%s)", BOT_BOTDROPPED,
  		 dcc[idx].nick, bots, (bots != 1) ? "s" : "", users,
		 (users != 1) ? "s" : "");
  chatout("*** %s\n", s);
  botnet_send_unlinked(idx, dcc[idx].nick, s);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

void restart_chons()
{
  int i;

  /* Dump party line members */
  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type == &DCC_CHAT) {
      check_bind_chon(dcc[i].nick, i);
      check_bind_chjn(conf.bot->nick, dcc[i].nick, dcc[i].u.chat->channel,
		     geticon(i), dcc[i].sock, dcc[i].host);
    }
  }
  for (i = 0; i < parties; i++) {
    check_bind_chjn(party[i].bot, party[i].nick, party[i].chan,
		   party[i].flag, party[i].sock, party[i].from);
  }
}
static int get_role(char *bot)
{
  int rl,
    i;
  struct bot_addr *ba;
  int r[5] = { 0, 0, 0, 0, 0 };
  struct userrec *u,
   *u2;

  u2 = get_user_by_handle(userlist, bot);
  if (!u2)
    return 1;

  for (u = userlist; u; u = u->next) {
    if (u->flags & USER_BOT) {
      if (strcmp(u->handle, bot)) {
        ba = get_user(&USERENTRY_BOTADDR, u);
        if ((nextbot(u->handle) >= 0) && (ba) && (ba->roleid > 0)
            && (ba->roleid < 5))
          r[(ba->roleid - 1)]++;
      }
    }
  }
  rl = 0;
  for (i = 1; i <= 4; i++)
    if (r[i] < r[rl])
      rl = i;
  rl++;
  ba = get_user(&USERENTRY_BOTADDR, u2);
  if (ba)
    ba->roleid = rl;
  return rl;
}

void lower_bot_linked(int idx)
{
  char tmp[6];
  sprintf(tmp, STR("rl %d"), get_role(dcc[idx].nick));
  botnet_send_zapf(nextbot(dcc[idx].nick), conf.bot->nick, dcc[idx].nick, tmp);

}

void higher_bot_linked(int idx)
{

}
