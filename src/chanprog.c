/*
 * chanprog.c -- handles:
 *   rmspace()
 *   maintaining the server list
 *   revenge punishment
 *   timers, utimers
 *   telling the current programmed settings
 *   initializing a lot of stuff and loading the tcl scripts
 *
 * $Id$
 */

#include "common.h"
#include "chanprog.h"
#include "settings.h"
#include "src/mod/channels.mod/channels.h"
#include "rfc1459.h"
#include "net.h"
#include "misc.h"
#include "users.h"
#include "userrec.h"
#include "main.h"
#include "debug.h"
#include "dccutil.h"
#include "botmsg.h"
#if HAVE_GETRUSAGE
#include <sys/resource.h>
#if HAVE_SYS_RUSAGE_H
#include <sys/rusage.h>
#endif
#endif
#include <sys/utsname.h>
#include "hooks.h"

struct chanset_t 	*chanset = NULL;	/* Channel list			*/
char 			admin[121] = "";	/* Admin info			*/
char 			origbotname[NICKLEN + 1] = "";
char 			botname[NICKLEN + 1] = "";	/* Primary botname		*/

#ifdef HUB
int     		my_port;
#endif /* HUB */


/* Remove space characters from beginning and end of string
 * (more efficent by Fred1)
 */
void rmspace(char *s)
{
  char *p = NULL, *end = NULL;
  int len;

  if (!s || (s && !*s))
    return;

  /* Wipe end of string */
  end = s + strlen(s) - 1;
  for (p = end; ((egg_isspace(*p)) && (p >= s)); p--);
  if (p != end) *(p + 1) = 0;
  len = p+1 - s;
  for (p = s; ((egg_isspace(*p)) && (*p)); p++);
  len -= (p - s);
  if (p != s) {
    /* +1 to include the null in the copy */
    memmove(s, p, len + 1);
  }
}

/* Returns memberfields if the nick is in the member list.
 */
memberlist *ismember(struct chanset_t *chan, char *nick)
{
  register memberlist	*x = NULL;

  for (x = chan->channel.member; x && x->nick[0]; x = x->next)
    if (!rfc_casecmp(x->nick, nick))
      return x;
  return NULL;
}

/* Find a chanset by channel name as the server knows it (ie !ABCDEchannel)
 */
struct chanset_t *findchan(const char *name)
{
  register struct chanset_t	*chan = NULL;

  for (chan = chanset; chan; chan = chan->next)
    if (!rfc_casecmp(chan->name, name))
      return chan;
  return NULL;
}

/* Find a chanset by display name (ie !channel)
 */
struct chanset_t *findchan_by_dname(const char *name)
{
  register struct chanset_t	*chan = NULL;

  for (chan = chanset; chan; chan = chan->next)
    if (!rfc_casecmp(chan->dname, name))
      return chan;
  return NULL;
}

/*
 *    "caching" functions
 */

/* Shortcut for get_user_by_host -- might have user record in one
 * of the channel caches.
 */
struct userrec *check_chanlist(const char *host)
{
  char				*nick = NULL, *uhost = NULL, buf[UHOSTLEN] = "";
  register memberlist		*m = NULL;
  register struct chanset_t	*chan = NULL;

  strncpyz(buf, host, sizeof buf);
  uhost = buf;
  nick = splitnick(&uhost);
  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) 
      if (!rfc_casecmp(nick, m->nick) && !egg_strcasecmp(uhost, m->userhost))
	return m->user;
  return NULL;
}

/* Shortcut for get_user_by_handle -- might have user record in channels
 */
struct userrec *check_chanlist_hand(const char *hand)
{
  register struct chanset_t	*chan = NULL;
  register memberlist		*m = NULL;

  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next)
      if (m->user && !egg_strcasecmp(m->user->handle, hand))
	return m->user;
  return NULL;
}

/* Clear the user pointers in the chanlists.
 *
 * Necessary when a hostmask is added/removed, a user is added or a new
 * userfile is loaded.
 */
void clear_chanlist(void)
{
  register memberlist		*m = NULL;
  register struct chanset_t	*chan = NULL;

  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      m->user = NULL;
      m->tried_getuser = 0;
    }

}

/* Clear the user pointer of a specific nick in the chanlists.
 *
 * Necessary when a hostmask is added/removed, a nick changes, etc.
 * Does not completely invalidate the channel cache like clear_chanlist().
 */
void clear_chanlist_member(const char *nick)
{
  register memberlist		*m = NULL;
  register struct chanset_t	*chan = NULL;

  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next)
      if (!rfc_casecmp(m->nick, nick)) {
	m->user = NULL;
        m->tried_getuser = 0;
	break;
      }
}

/* If this user@host is in a channel, set it (it was null)
 */
void set_chanlist(const char *host, struct userrec *rec)
{
  char				*nick = NULL, *uhost = NULL, buf[UHOSTLEN] = "";
  register memberlist		*m = NULL;
  register struct chanset_t	*chan = NULL;

  strncpyz(buf, host, sizeof buf);
  uhost = buf;
  nick = splitnick(&uhost);
  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next)
      if (!rfc_casecmp(nick, m->nick) && !egg_strcasecmp(uhost, m->userhost))
	m->user = rec;
}

/* 0 marks all channels
 * 1 removes marked channels
 * 2 unmarks all channels
 */

void checkchans(int which)
{
  struct chanset_t *chan = NULL, *chan_next = NULL;

  sdprintf("checkchans(%d)", which);
  if (which == 0 || which == 2) {
    for (chan = chanset; chan; chan = chan->next) {
      if (which == 0) {
        chan->status |= CHAN_FLAGGED;
      } else if (which == 2) {
        chan->status &= ~CHAN_FLAGGED;
      }
    }
  } else if (which == 1) {
    for (chan = chanset; chan; chan = chan_next) {
      chan_next = chan->next;
      if (chan->status & CHAN_FLAGGED) {
        putlog(LOG_MISC, "*", "No longer supporting channel %s", chan->dname);
        remove_channel(chan);
      }
    }
  }

}

/* Dump uptime info out to dcc (guppy 9Jan99)
 */
void tell_verbose_uptime(int idx)
{
  char s[256] = "", s1[121] = "";
  time_t now2, hr, min;

  now2 = now - online_since;
  s[0] = 0;
  if (now2 > 86400) {
    /* days */
    sprintf(s, "%d day", (int) (now2 / 86400));
    if ((int) (now2 / 86400) >= 2)
      strcat(s, "s");
    strcat(s, ", ");
    now2 -= (((int) (now2 / 86400)) * 86400);
  }
  hr = (time_t) ((int) now2 / 3600);
  now2 -= (hr * 3600);
  min = (time_t) ((int) now2 / 60);
  sprintf(&s[strlen(s)], "%02d:%02d", (int) hr, (int) min);
  s1[0] = 0;
  if (backgrd)
    strcpy(s1, MISC_BACKGROUND);
  else {
    if (term_z)
      strcpy(s1, MISC_TERMMODE);
    else
      strcpy(s1, MISC_LOGMODE);
  }
  dprintf(idx, "%s %s  (%s)\n", MISC_ONLINEFOR, s, s1);
}

/* Dump status info out to dcc
 */
void tell_verbose_status(int idx)
{
  char s[256] = "", s1[121] = "", s2[81] = "", *vers_t = NULL, *uni_t = NULL;
#ifdef HUB
  int i;
#endif /* HUB */
  time_t now2, hr, min;
#if HAVE_GETRUSAGE
  struct rusage ru;
#else
# if HAVE_CLOCK
  clock_t cl;
# endif
#endif /* HAVE_GETRUSAGE */
  struct utsname un;

  if (!uname(&un) < 0) {
    vers_t = " ";
    uni_t = "*unknown*";
  } else {
    vers_t = un.release;
    uni_t = un.sysname;
  }

#ifdef HUB
  i = count_users(userlist);
  dprintf(idx, "I am %s, running %s:  %d user%s\n",
	  conf.bot->nick, ver, i, i == 1 ? "" : "s");
  if (localhub)
    dprintf(idx, "I am a localhub.\n");
  if (isupdatehub())
    dprintf(idx, "I am an update hub.\n");
#endif /* HUB */
  now2 = now - online_since;
  s[0] = 0;
  if (now2 > 86400) {
    /* days */
    sprintf(s, "%d day", (int) (now2 / 86400));
    if ((int) (now2 / 86400) >= 2)
      strcat(s, "s");
    strcat(s, ", ");
    now2 -= (((int) (now2 / 86400)) * 86400);
  }
  hr = (time_t) ((int) now2 / 3600);
  now2 -= (hr * 3600);
  min = (time_t) ((int) now2 / 60);
  sprintf(&s[strlen(s)], "%02d:%02d", (int) hr, (int) min);
  s1[0] = 0;
  if (backgrd)
    strcpy(s1, MISC_BACKGROUND);
  else {
    if (term_z)
      strcpy(s1, MISC_TERMMODE);
    else
      strcpy(s1, MISC_LOGMODE);
  }
#if HAVE_GETRUSAGE
  getrusage(RUSAGE_SELF, &ru);
  hr = (int) ((ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) / 60);
  min = (int) ((ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) - (hr * 60));
  sprintf(s2, "CPU %02d:%02d", (int) hr, (int) min);	/* Actally min/sec */
#else
# if HAVE_CLOCK
  cl = (clock() / CLOCKS_PER_SEC);
  hr = (int) (cl / 60);
  min = (int) (cl - (hr * 60));
  sprintf(s2, "CPU %02d:%02d", (int) hr, (int) min);	/* Actually min/sec */
# else
  sprintf(s2, "CPU ???");
# endif
#endif /* HAVE_GETRUSAGE */
  dprintf(idx, "%s %s  (%s)  %s  %s %4.1f%%\n", MISC_ONLINEFOR,
	  s, s1, s2, MISC_CACHEHIT,
	  100.0 * ((float) cache_hit) / ((float) (cache_hit + cache_miss)));

  if (admin[0])
    dprintf(idx, "Admin: %s\n", admin);

  dprintf(idx, "OS: %s %s\n", uni_t, vers_t);
}

/* Show all internal state variables
 */
void tell_settings(int idx)
{
  char s[1024] = "";
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  dprintf(idx, "Botnet Nickname: %s\n", conf.bot->nick);
  if (firewall[0])
    dprintf(idx, "Firewall: %s, port %d\n", firewall, firewallport);
#ifdef HUB
  dprintf(idx, "Userfile: %s   \n", userfile);
#endif /* HUB */
  dprintf(idx, "Directories:\n");
  dprintf(idx, "  Temp    : %s\n", tempdir);
  fr.global = default_flags;

  build_flags(s, &fr, NULL);
  dprintf(idx, "%s [%s]\n", MISC_NEWUSERFLAGS, s);
#ifdef HUB
  if (owner[0])
    dprintf(idx, "%s: %s\n", MISC_PERMOWNER, owner);
#endif /* HUB */
  dprintf(idx, "Ignores last %d mins\n", ignore_time);
}

void reaffirm_owners()
{
  char *p = NULL, *q = NULL, s[121] = "";
  struct userrec *u = NULL;

  /* Make sure perm owners are +a */
  if (owner[0]) {
    q = owner;
    p = strchr(q, ',');
    while (p) {
      strncpyz(s, q, (p - q) + 1);
      rmspace(s);
      u = get_user_by_handle(userlist, s);
      if (u)
	u->flags = sanity_check(u->flags | USER_ADMIN);
      q = p + 1;
      p = strchr(q, ',');
    }
    strcpy(s, q);
    rmspace(s);
    u = get_user_by_handle(userlist, s);
    if (u)
      u->flags = sanity_check(u->flags | USER_ADMIN);
  }
}

void load_internal_users()
{
  char *p = NULL, *ln = NULL, *hand = NULL, *ip = NULL, *port = NULL, *pass = NULL;
  char *hosts = NULL, host[UHOSTMAX] = "", buf[2048] = "", *attr = NULL;
  int i, hublevel = 0;
  struct bot_addr *bi = NULL;
  struct userrec *u = NULL;

  /* hubs */
  egg_snprintf(buf, sizeof buf, "%s", hubs);
  p = buf;
  while (p) {
    ln = p;
    p = strchr(p, ',');
    if (p)
      *p++ = 0;
    hand = ln;
    ip = NULL;
    port = NULL;
    hosts = NULL;
    for (i = 0; ln; i++) {
      switch (i) {
      case 0:
	hand = ln;
	break;
      case 1:
	ip = ln;
	break;
      case 2:
	port = ln;
	break;
      case 3:
        hublevel++;		/* We must increment this even if it is already added */
	if (!get_user_by_handle(userlist, hand)) {
	  userlist = adduser(userlist, hand, "none", "-", USER_BOT | USER_OP);
	  bi = calloc(1, sizeof(struct bot_addr));

          bi->address = strdup(ip);
	  bi->telnet_port = atoi(port) ? atoi(port) : 0;
	  bi->relay_port = bi->telnet_port;
          bi->hublevel = hublevel;
#ifdef HUB
	  if ((!bi->hublevel) && (!strcmp(hand, conf.bot->nick)))
	    bi->hublevel = 99;
#endif /* HUB */
          bi->uplink = calloc(1, 1);
	  set_user(&USERENTRY_BOTADDR, get_user_by_handle(userlist, hand), bi);
	  /* set_user(&USERENTRY_PASS, get_user_by_handle(userlist, hand), SALT2); */
	}
      default:
	/* ln = userids for hostlist, add them all */
	hosts = ln;
	ln = strchr(ln, ' ');
	if (ln)
	  *ln++ = 0;
	while (hosts) {
	  egg_snprintf(host, sizeof host, "*!%s@%s", hosts, ip);
	  set_user(&USERENTRY_HOSTS, get_user_by_handle(userlist, hand), host);
	  hosts = ln;
	  if (ln)
	    ln = strchr(ln, ' ');
	  if (ln)
	    *ln++ = 0;
	}
	break;
      }
      if (ln)
	ln = strchr(ln, ' ');
      if (ln) {
	*ln++ = 0;
      }
    }
  }

  /* perm owners */
  owner[0] = 0;

  egg_snprintf(buf, sizeof buf, "%s", owners);
  p = buf;
  while (p) {
    ln = p;
    p = strchr(p, ',');
    if (p)
      *p++ = 0;
    hand = ln;
    pass = NULL;
    attr = NULL;
    hosts = NULL;
    for (i = 0; ln; i++) {
      switch (i) {
      case 0:
	hand = ln;
	break;
      case 1:
        pass = ln;
        break;
      case 2:
	hosts = ln;
	if (owner[0])
	  strncat(owner, ",", 120);
	strncat(owner, hand, 120);
	if (!get_user_by_handle(userlist, hand)) {
	  userlist = adduser(userlist, hand, "none", "-", USER_ADMIN | USER_OWNER | USER_MASTER | USER_OP | USER_PARTY | USER_HUBA | USER_CHUBA);
	  u = get_user_by_handle(userlist, hand);
	  set_user(&USERENTRY_PASS, u, pass);
	  while (hosts) {
	    ln = strchr(ln, ' ');
	    if (ln)
	      *ln++ = 0;
	    set_user(&USERENTRY_HOSTS, u, hosts);
	    hosts = ln;
	  }
	}
	break;
      }
      if (ln)
	ln = strchr(ln, ' ');
      if (ln)
	*ln++ = 0;
    }
  }

}

void chanprog()
{
  /* char buf[2048] = ""; */
  struct bot_addr *bi = NULL;

  admin[0] = 0;
  /* cache our ip on load instead of every 30 seconds */
  cache_my_ip();
  sdprintf("ip4: %s", myipstr(4));
  sdprintf("ip6: %s", myipstr(6));
  conmask = 0;

 /* now this only checks server shit. (no channels) */
  call_hook(HOOK_REHASH);

  strcpy(botuser, origbotname);

  if (!conf.bot->nick)
    fatal("I don't have a nickname!!\n", 0);
#ifdef HUB
  loading = 1;
  checkchans(0);
  readuserfile(userfile, &userlist);
  checkchans(1);
  loading = 0;
#endif /* HUB */

  load_internal_users();

  if (!(conf.bot->u = get_user_by_handle(userlist, conf.bot->nick))) {
    /* I need to be on the userlist... doh. */
    userlist = adduser(userlist, conf.bot->nick, "none", "-", USER_BOT | USER_OP );
    conf.bot->u = get_user_by_handle(userlist, conf.bot->nick);
    bi = calloc(1, sizeof(struct bot_addr));
    if (conf.bot->ip)
      bi->address = strdup(conf.bot->ip);
    /* bi->telnet_port = atoi(buf) ? atoi(buf) : 3333; */
    bi->telnet_port = bi->relay_port = 3333;
#ifdef HUB
    bi->hublevel = 99;
#else /* !HUB */
    bi->hublevel = 0;
#endif /* HUB */
    bi->uplink = calloc(1, 1);
    set_user(&USERENTRY_BOTADDR, conf.bot->u, bi);
  } else {
    bi = get_user(&USERENTRY_BOTADDR, conf.bot->u);
  }

  bi = get_user(&USERENTRY_BOTADDR, get_user_by_handle(userlist, conf.bot->nick));
  if (!bi)
    fatal(STR("I'm added to userlist but without a bot record!"), 0);
  if (bi->telnet_port != 3333) {
#ifdef HUB
    listen_all(bi->telnet_port, 0);
    my_port = bi->telnet_port;
#endif /* HUB */
  }

  trigger_cfg_changed();

  /* We should be safe now */

  reaffirm_owners();
}

#ifdef HUB
/* Reload the user file from disk
 */
void reload()
{
  FILE *f = NULL;

  f = fopen(userfile, "r");
  if (f == NULL) {
    putlog(LOG_MISC, "*", MISC_CANTRELOADUSER);
    return;
  }
  fclose(f);
  noshare = 1;
  clear_userlist(userlist);
  noshare = 0;
  userlist = NULL;
  loading = 1;
  checkchans(0);
  if (!readuserfile(userfile, &userlist))
    fatal(MISC_MISSINGUSERF, 0);
  checkchans(1);
  loading = 0;
  reaffirm_owners();
  call_hook(HOOK_READ_USERFILE);
}
#endif /* HUB */

void rehash()
{
  call_hook(HOOK_PRE_REHASH);
  noshare = 1;
  clear_userlist(userlist);
  noshare = 0;
  userlist = NULL;
  chanprog();
}

/* Oddly enough, written by proton (Emech's coder)
 */
int isowner(char *name)
{
  char *pa = NULL, *pb = NULL;
  char nl, pl;

  if (!owner || !*owner)
    return (0);
  if (!name || !*name)
    return (0);
  nl = strlen(name);
  pa = owner;
  pb = owner;
  while (1) {
    while (1) {
      if ((*pb == 0) || (*pb == ',') || (*pb == ' '))
	break;
      pb++;
    }
    pl = (unsigned int) pb - (unsigned int) pa;
    if (pl == nl && !egg_strncasecmp(pa, name, nl))
      return (1);
    while (1) {
      if ((*pb == 0) || ((*pb != ',') && (*pb != ' ')))
	break;
      pb++;
    }
    if (*pb == 0)
      return (0);
    pa = pb;
  }
}

int shouldjoin(struct chanset_t *chan)
{
  if (!strncmp(conf.bot->nick, "wtest", 5) && !strcmp(chan->dname, "#wraith"))
    return 1;
  else if (!strncmp(conf.bot->nick, "wtest", 4)) /* use 5 for all */
    return 0; 
#ifdef G_BACKUP
  struct flag_record fr = { FR_CHAN | FR_ANYWH | FR_GLOBAL, 0, 0, 0, 0 };
 
  if (!chan || (chan && !chan->name || (chan->name && !chan->name[0])))
    return 0;

  get_user_flagrec(get_user_by_handle(userlist, conf.bot->nick), &fr, chan->name);
  return (!channel_inactive(chan)
          && (channel_backup(chan) || !glob_backupbot(fr)));
#else /* !G_BACKUP */
  return !channel_inactive(chan);
#endif /* G_BACKUP */
}

/* do_chanset() set (options) on (chan)
 * USES DO_LOCAL|DO_NET bits.
 */
int do_chanset(char *result, struct chanset_t *chan, char *options, int local)
{
  int ret = OK;

  if (local & DO_NET) {
    char *buf = NULL;
         /* malloc(options,chan,'cset ',' ',+ 1) */
    if (chan)
      buf = calloc(1, strlen(options) + strlen(chan->dname) + 5 + 1 + 1);
    else
      buf = calloc(1, strlen(options) + 1 + 5 + 1 + 1);

    strcat(buf, "cset ");
    if (chan)
      strcat(buf, chan->dname);
    else
      strcat(buf, "*");
    strcat(buf, " ");
    strcat(buf, options);
    buf[strlen(buf)] = 0;
    putlog(LOG_DEBUG, "*", "sending out cset: %s", buf);
    putallbots(buf); 
    free(buf);
  }

  if (local & DO_LOCAL) {
    struct chanset_t *ch = NULL;
    int all = chan ? 0 : 1;

    if (chan)
      ch = chan;
    else
      ch = chanset;

    while (ch) {
      const char **item = NULL;
      int items = 0;

      if (SplitList(result, options, &items, &item) == OK) {
        ret = channel_modify(result, ch, items, (char **) item);
      } else 
        ret = ERROR;


      free(item);

      if (all) {
        if (ret == ERROR) /* just bail if there was an error, no sense in trying more */
          return ret;

        ch = ch->next;
      } else {
        ch = NULL;
      }
    }
  }
  return ret;
}
