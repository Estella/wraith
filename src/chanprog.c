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

#include "main.h"
#if HAVE_GETRUSAGE
#include <sys/resource.h>
#if HAVE_SYS_RUSAGE_H
#include <sys/rusage.h>
#endif
#endif
#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif
#include "modules.h"

extern struct userrec	*userlist;
extern log_t		*logs;
extern Tcl_Interp	*interp;
extern char		 ver[], botnetnick[], firewall[], myip[], origbotname[],
			 motdfile[], userfile[], helpdir[], tempdir[],
			 notify_new[], owner[], configfile[],
                         netpass[], botuser[], owners[], hubs[];

extern time_t		 now, online_since;
extern int		 backgrd, term_z, con_chan, cache_hit, cache_miss,
			 firewallport, default_flags, max_logs, conmask,
			 protect_readonly, noshare,
#ifdef HUB
			 my_port,
#endif
			 ignore_time, loading;

tcl_timer_t	 *timer = NULL;		/* Minutely timer		*/
tcl_timer_t	 *utimer = NULL;	/* Secondly timer		*/
unsigned long	  timer_id = 1;		/* Next timer of any sort will
					   have this number		*/
struct chanset_t *chanset = NULL;	/* Channel list			*/
char		  admin[121] = "";	/* Admin info			*/
char		  origbotname[NICKLEN + 1];
char		  botname[NICKLEN + 1];	/* Primary botname		*/


/* Remove space characters from beginning and end of string
 * (more efficent by Fred1)
 */
void rmspace(char *s)
{
#define whitespace(c) (((c) == 32) || ((c) == 9) || ((c) == 13) || ((c) == 10))
  char *p;

  if (*s == '\0')
	return;

  /* Wipe end of string */
  for (p = s + strlen(s) - 1; ((whitespace(*p)) && (p >= s)); p--);
  if (p != s + strlen(s) - 1)
    *(p + 1) = 0;
  for (p = s; ((whitespace(*p)) && (*p)); p++);
  if (p != s)
    strcpy(s, p);
}

/* Returns memberfields if the nick is in the member list.
 */
memberlist *ismember(struct chanset_t *chan, char *nick)
{
  register memberlist	*x;

  for (x = chan->channel.member; x && x->nick[0]; x = x->next)
    if (!rfc_casecmp(x->nick, nick))
      return x;
  return NULL;
}

/* Find a chanset by channel name as the server knows it (ie !ABCDEchannel)
 */
struct chanset_t *findchan(const char *name)
{
  register struct chanset_t	*chan;

  for (chan = chanset; chan; chan = chan->next)
    if (!rfc_casecmp(chan->name, name))
      return chan;
  return NULL;
}

/* Find a chanset by display name (ie !channel)
 */
struct chanset_t *findchan_by_dname(const char *name)
{
  register struct chanset_t	*chan;

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
  char				*nick, *uhost, buf[UHOSTLEN];
  register memberlist		*m;
  register struct chanset_t	*chan;

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
  register struct chanset_t	*chan;
  register memberlist		*m;

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
  register memberlist		*m;
  register struct chanset_t	*chan;

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
  register memberlist		*m;
  register struct chanset_t	*chan;

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
  char				*nick, *uhost, buf[UHOSTLEN];
  register memberlist		*m;
  register struct chanset_t	*chan;

  strncpyz(buf, host, sizeof buf);
  uhost = buf;
  nick = splitnick(&uhost);
  for (chan = chanset; chan; chan = chan->next)
    for (m = chan->channel.member; m && m->nick[0]; m = m->next)
      if (!rfc_casecmp(nick, m->nick) && !egg_strcasecmp(uhost, m->userhost))
	m->user = rec;
}

/* Calculate the memory we should be using
 */
int expmem_chanprog()
{
  register int		 tot = 0;
  register tcl_timer_t	*t;

  for (t = timer; t; t = t->next)
    tot += sizeof(tcl_timer_t) + strlen(t->cmd) + 1;
  for (t = utimer; t; t = t->next)
    tot += sizeof(tcl_timer_t) + strlen(t->cmd) + 1;
  return tot;
}

/* Dump uptime info out to dcc (guppy 9Jan99)
 */
void tell_verbose_uptime(int idx)
{
  char s[256], s1[121];
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
    else if (con_chan)
      strcpy(s1, MISC_STATMODE);
    else
      strcpy(s1, MISC_LOGMODE);
  }
  dprintf(idx, "%s %s  (%s)\n", MISC_ONLINEFOR, s, s1);
}

/* Dump status info out to dcc
 */
void tell_verbose_status(int idx)
{
  char s[256], s1[121], s2[81];
  char *vers_t, *uni_t;
#ifdef HUB
  int i;
#endif
  time_t now2, hr, min;
#if HAVE_GETRUSAGE
  struct rusage ru;
#else
# if HAVE_CLOCK
  clock_t cl;
# endif
#endif
#ifdef HAVE_UNAME
  struct utsname un;

  if (!uname(&un) < 0) {
#endif
    vers_t = " ";
    uni_t = "*unknown*";
#ifdef HAVE_UNAME
  } else {
    vers_t = un.release;
    uni_t = un.sysname;
  }
#endif

#ifdef HUB
  i = count_users(userlist);
  dprintf(idx, "I am %s, running %s:  %d user%s (mem: %uk)\n",
	  botnetnick, ver, i, i == 1 ? "" : "s",
          (int) (expected_memory() / 1024));
#endif
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
    else if (con_chan)
      strcpy(s1, MISC_STATMODE);
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
#endif
  dprintf(idx, "%s %s  (%s)  %s  %s %4.1f%%\n", MISC_ONLINEFOR,
	  s, s1, s2, MISC_CACHEHIT,
	  100.0 * ((float) cache_hit) / ((float) (cache_hit + cache_miss)));

  if (admin[0])
    dprintf(idx, "Admin: %s\n", admin);

  dprintf(idx, "OS: %s %s\n", uni_t, vers_t);

  /* info library */
  dprintf(idx, "%s %s\n", MISC_TCLLIBRARY,
	  ((interp) && (Tcl_Eval(interp, "info library") == TCL_OK)) ?
	  interp->result : "*unknown*");

  /* info tclversion/patchlevel */
  dprintf(idx, "%s %s (%s %s)\n", MISC_TCLVERSION,
	  ((interp) && (Tcl_Eval(interp, "info patchlevel") == TCL_OK)) ?
	  interp->result : (Tcl_Eval(interp, "info tclversion") == TCL_OK) ?
	  interp->result : "*unknown*", MISC_TCLHVERSION,
	  TCL_PATCH_LEVEL ? TCL_PATCH_LEVEL : "*unknown*");
#if HAVE_TCL_THREADS
  dprintf(idx, "Tcl is threaded\n");
#endif  
	  
}

/* Show all internal state variables
 */
void tell_settings(int idx)
{
  char s[1024];
  int i;
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  dprintf(idx, "Botnet Nickname: %s\n", botnetnick);
  if (firewall[0])
    dprintf(idx, "Firewall: %s, port %d\n", firewall, firewallport);
#ifdef HUB
  dprintf(idx, "Userfile: %s   \n", userfile);
#endif
  dprintf(idx, "Directories:\n");
  dprintf(idx, "  Help    : %s\n", helpdir);
  dprintf(idx, "  Temp    : %s\n", tempdir);
  fr.global = default_flags;

  build_flags(s, &fr, NULL);
  dprintf(idx, "%s [%s], %s: %s\n", MISC_NEWUSERFLAGS, s,
	  MISC_NOTIFY, notify_new);
#ifdef HUB
  if (owner[0])
    dprintf(idx, "%s: %s\n", MISC_PERMOWNER, owner);
#endif
  for (i = 0; i < max_logs; i++)
    if (logs[i].filename != NULL) {
      dprintf(idx, "Logfile #%d: %s on %s (%s: %s)\n", i + 1,
	      logs[i].filename, logs[i].chname,
	      masktype(logs[i].mask), maskname(logs[i].mask));
    }
  dprintf(idx, "Ignores last %d mins\n", ignore_time);
}

void reaffirm_owners()
{
  char *p, *q, s[121];
  struct userrec *u;

  /* Make sure default owners are +a */
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
  char *p = NULL,
   *ln,
   *hand,
   *ip,
   *port,
   *hublevel = NULL,
   *pass,
   *hosts,
    host[250],
    buf[2048];
  char *attr;
  int i;
  struct bot_addr *bi;
  struct userrec *u;
  //struct flag_record fr = {FR_BOT, 0, 0, 0, 0, 0};

//hubs..
  sprintf(buf, "%s", hubs);
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
        hublevel = ln;
        break;
      case 4:
	if (!get_user_by_handle(userlist, hand)) {
	  userlist = adduser(userlist, hand, "none", "-", USER_BOT | USER_OP | USER_FRIEND);
/* no thanks.
          if (atoi(hublevel) < 999 && strcmp(hand, origbotname)) {
            u = get_user_by_handle(userlist, hand);
            fr.bot |= (BOT_PASSIVE | BOT_GLOBAL);
//            set_user(&USERENTRY_BOTFL, u, (void *) fr.bot);
            set_user_flagrec(u, &fr, NULL);
          }
//          user.bot = BITS;
*/
	  bi = user_malloc(sizeof(struct bot_addr));

	  bi->address = user_malloc(strlen(ip) + 1);
	  strcpy(bi->address, ip);
	  bi->telnet_port = atoi(port) ? atoi(port) : 0;
	  bi->relay_port = bi->telnet_port;
          bi->hublevel = atoi(hublevel);
#ifdef HUB
//printf("adding %s with hublevel: %d\n", hand, bi->hublevel ? bi->hublevel : 99);

	  if ((!bi->hublevel) && (!strcmp(hand, botnetnick)))
	    bi->hublevel = 99;
#endif
          bi->uplink = user_malloc(1);
          bi->uplink[0] = 0;
	  set_user(&USERENTRY_BOTADDR, get_user_by_handle(userlist, hand), bi);
	  set_user(&USERENTRY_PASS, get_user_by_handle(userlist, hand), netpass);
	}
      default:
//	ln = userids for hostlist, add them all 
	hosts = ln;
	ln = strchr(ln, ' ');
	if (ln)
	  *ln++ = 0;
	while (hosts) {
	  sprintf(host, "*!%s@%s", hosts, ip);
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

//owners..
  owner[0] = 0;
//  strcpy(p, owners);

// buf = get_setting(1);
  sprintf(buf, "%s", owners);
  p = buf;
  while (p) {
    ln = p;
    p = strchr(p, ',');
    if (p)
      *p++ = 0;
//     name pass hostlist 
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
	  userlist = adduser(userlist, hand, "none", "-", USER_ADMIN | USER_OWNER | USER_MASTER | USER_FRIEND | USER_OP | USER_PARTY | USER_HUBA | USER_CHUBA);
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
  int i;
  char buf[2048];
  struct bot_addr *bi;
  struct userrec *u;


  admin[0] = 0;
  helpdir[0] = 0;
  for (i = 0; i < max_logs; i++)
    logs[i].flags |= LF_EXPIRING;
  conmask = 0;
  /* Turn off read-only variables (make them write-able) for rehash */
  protect_readonly = 0;
  /* Now read it */
  if (configfile[0])
    if (!readtclprog(configfile))
      fatal(MISC_NOCONFIGFILE, 0);

//now this only checks server shit. (no channels)
  call_hook(HOOK_REHASH);
  protect_readonly = 1;
  if (!botnetnick[0]) {
    strncpyz(botnetnick, origbotname, HANDLEN + 1);
  }
  strcpy(botuser, origbotname);
  if (!botnetnick[0])
    fatal("I don't have a botnet nick!!\n", 0);
#ifdef HUB
  if (!userfile[0])
    fatal(MISC_NOUSERFILE2, 0);
  //setstatic = 0;
  loading = 1;
  readuserfile(userfile, &userlist);
/* old
  if (!readuserfile(userfile, &userlist)) {
   char tmp[178];
   egg_snprintf(tmp, sizeof tmp, MISC_NOUSERFILE, configfile);
   fatal(tmp, 0);
  }
*/
  loading = 0;
  //setstatic = 1;
#endif

  load_internal_users();

  if (!(u = get_user_by_handle(userlist, botnetnick))) {
    /* I need to be on the userlist... doh. */
    userlist = adduser(userlist, botnetnick, STR("none"), "-", USER_BOT | USER_OP | USER_FRIEND);
    u = get_user_by_handle(userlist, botnetnick);
    bi = user_malloc(sizeof(struct bot_addr));

    bi->address = user_malloc(strlen(myip) + 1);
    strcpy(bi->address, myip);
    bi->telnet_port = atoi(buf) ? atoi(buf) : 3333;
    bi->relay_port = bi->telnet_port;
#ifdef HUB
    bi->hublevel = 99;
#else
    bi->hublevel = 0;
#endif
    bi->uplink = user_malloc(1);
    bi->uplink[0] = 0;
    set_user(&USERENTRY_BOTADDR, u, bi);
  } else {
    bi = get_user(&USERENTRY_BOTADDR, u);
  }

  for (i = 0; i < max_logs; i++) {
    if (logs[i].flags & LF_EXPIRING) {
      if (logs[i].filename != NULL) {
        nfree(logs[i].filename);
        logs[i].filename = NULL;
      }
      if (logs[i].chname != NULL) {
        nfree(logs[i].chname);
        logs[i].chname = NULL;
      }
      if (logs[i].f != NULL) {
        fclose(logs[i].f);
        logs[i].f = NULL;
      }
      logs[i].mask = 0;
      logs[i].flags = 0;
    }
  }

  bi = get_user(&USERENTRY_BOTADDR, get_user_by_handle(userlist, botnetnick));
  if (!bi)
    fatal(STR("I'm added to userlist but without a bot record!"), 0);
  if (bi->telnet_port != 3333) {
#ifdef HUB
    listen_all(bi->telnet_port, 0);
    my_port = bi->telnet_port;
#endif
  }

  trigger_cfg_changed();

  /* We should be safe now */


  if (helpdir[0])
    if (helpdir[strlen(helpdir) - 1] != '/')
      strcat(helpdir, "/");

  if (tempdir[0])
    if (tempdir[strlen(tempdir) - 1] != '/')
      strcat(tempdir, "/");


  /* test tempdir: it's vital */
  {
    FILE *f;
    char s[161];
    int fd;

    /* possible file race condition solved by using a random string
     * and the process id in the filename */
    /* Let's not even dare to hope... use mkstemp() -dizz */
    sprintf(s, STR("%s.test-XXXXXX"), tempdir);
    if ((fd = mkstemp(s)) == -1 || (f = fdopen(fd, "w")) == NULL) {
      if (fd != -1) {
        unlink(s);
        close(fd);
      }
      fatal(STR("Can't write to tempdir!"), 0);
    }
    unlink(s);
  }
  reaffirm_owners();
}
#ifdef HUB

/* Reload the user file from disk
 */
void reload()
{
  FILE *f;

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
  //setstatic = 0;
  loading = 1;
  if (!readuserfile(userfile, &userlist))
    fatal(MISC_MISSINGUSERF, 0);
  loading = 0;
  //setstatic = 1;
  reaffirm_owners();
  call_hook(HOOK_READ_USERFILE);
}
#endif


void rehash()
{
  call_hook(HOOK_PRE_REHASH);
#ifdef HUB
  noshare = 1;
  clear_userlist(userlist);
  noshare = 0;
  userlist = NULL;
#endif
  chanprog();
}

/*
 *    Brief venture into timers
 */

/* Add a timer
 */
unsigned long add_timer(tcl_timer_t **stack, int elapse, char *cmd,
			unsigned long prev_id)
{
  tcl_timer_t *old = (*stack);

  *stack = (tcl_timer_t *) nmalloc(sizeof(tcl_timer_t));
  (*stack)->next = old;
  (*stack)->mins = elapse;
  (*stack)->cmd = (char *) nmalloc(strlen(cmd) + 1);
  strcpy((*stack)->cmd, cmd);
  /* If it's just being added back and already had an id,
   * don't create a new one.
  */
  if (prev_id > 0)
    (*stack)->id = prev_id;
  else
    (*stack)->id = timer_id++;
  return (*stack)->id;
}

/* Remove a timer, by id
 */
int remove_timer(tcl_timer_t **stack, unsigned long id)
{
  tcl_timer_t *old;
  int ok = 0;

  while (*stack) {
    if ((*stack)->id == id) {
      ok++;
      old = *stack;
      *stack = ((*stack)->next);
      nfree(old->cmd);
      nfree(old);
    } else
      stack = &((*stack)->next);
  }
  return ok;
}

/* Check timers, execute the ones that have expired.
 */
void do_check_timers(tcl_timer_t **stack)
{
  tcl_timer_t *mark = *stack, *old = NULL;
  char x[16];

  /* New timers could be added by a Tcl script inside a current timer
   * so i'll just clear out the timer list completely, and add any
   * unexpired timers back on.
   */
  *stack = NULL;
  while (mark) {
    if (mark->mins > 0)
      mark->mins--;
    old = mark;
    mark = mark->next;
    if (!old->mins) {
      egg_snprintf(x, sizeof x, "timer%lu", old->id);
      do_tcl(x, old->cmd);
      nfree(old->cmd);
      nfree(old);
    } else {
      old->next = *stack;
      *stack = old;
    }
  }
}

/* Wipe all timers.
 */
void wipe_timers(Tcl_Interp *irp, tcl_timer_t **stack)
{
  tcl_timer_t *mark = *stack, *old;

  while (mark) {
    old = mark;
    mark = mark->next;
    nfree(old->cmd);
    nfree(old);
  }
  *stack = NULL;
}

/* Return list of timers
 */
void list_timers(Tcl_Interp *irp, tcl_timer_t *stack)
{
  tcl_timer_t *mark;
  char mins[10], id[16], *x;
#if (((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4)) || (TCL_MAJOR_VERSION > 8))
  CONST char *argv[3];
#else
  char *argv[3];
#endif
  for (mark = stack; mark; mark = mark->next) {
    egg_snprintf(mins, sizeof mins, "%u", mark->mins);
    egg_snprintf(id, sizeof id, "timer%lu", mark->id);
    argv[0] = mins;
    argv[1] = mark->cmd;
    argv[2] = id;
    x = Tcl_Merge(3, argv);
    Tcl_AppendElement(irp, x);
    Tcl_Free((char *) x);
  }
}

/* Oddly enough, written by proton (Emech's coder)
 */
int isowner(char *name)
{
  char *pa, *pb;
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

/* If we have a protected topic and the bot is opped
 * or the channel is -t, change the topic. (Sup 11May2001)
 */
void check_topic(struct chanset_t *chan)
{
  memberlist *m = NULL;  

  if (chan->topic_prot[0]) {
    m = ismember(chan, botname);
    if (!m)
      return;
    if (chan->channel.topic) {
      if (!egg_strcasecmp(chan->topic_prot, chan->channel.topic))
	return;
    }
    if (chan_hasop(m) || !channel_optopic(chan))
      dprintf(DP_SERVER, "TOPIC %s :%s\n", chan->name, chan->topic_prot);
  }
}
