/*
 * userrec.c -- handles:
 *   add_q() del_q() str2flags() flags2str() str2chflags() chflags2str()
 *   a bunch of functions to find and change user records
 *   change and check user (and channel-specific) flags
 *
 * $Id$
 */

#include <sys/stat.h>
#include "common.h"
#include "userrec.h"
#include "misc.h"
#include "misc_file.h"
#include "rfc1459.h"
#include "dcc.h"
#include "src/mod/share.mod/share.h"
#include "main.h"
#include "users.h"
#include "chan.h"
#include "match.h"
#include "dccutil.h"
#include "hooks.h"
#include "tandem.h"
#include "chanprog.h"
#include "crypt.h"
#include "core_binds.h"

int		 noshare = 1;		/* don't send out to sharebots	    */
struct userrec	*userlist = NULL;	/* user records are stored here	    */
struct userrec	*lastuser = NULL;	/* last accessed user record	    */
maskrec		*global_bans = NULL,
		*global_exempts = NULL,
		*global_invites = NULL;
struct igrec	*global_ign = NULL;
int		cache_hit = 0,
		cache_miss = 0;		/* temporary cache accounting	    */
int		strict_host = 0;
int		userfile_perm = 0600;	/* Userfile permissions,
					   default rw-------		    */


static int		 sort_users = 1;	/* sort the userlist when saving    */

int count_users(struct userrec *bu)
{
  int tot = 0;
  struct userrec *u = NULL;

  for (u = bu; u; u = u->next)
    tot++;
  return tot;
}

/* Convert "nick!~user@host", "nick!+user@host" and "nick!-user@host"
 * to "nick!user@host" if necessary. (drummer)
 */
char *fixfrom(char *s)
{
  static char buf[512] = "";
  char *p = NULL;

  if (s == NULL)
    return NULL;
  strncpyz(buf, s, sizeof buf);
  if (strict_host)
    return buf;
  if ((p = strchr(buf, '!')))
    p++;
  else
    p = s;			/* Sometimes we get passed just a
				 * user@host here... */
  /* These are ludicrous. */
  if (strchr("~+-^=", *p) && (p[1] != '@')) /* added check for @ - drummer */
/*    strcpy(p, p + 1); */
    sprintf(p, "%s", p + 1);
  /* Bug was: n!~@host -> n!@host  now: n!~@host */
  return buf;
}

struct userrec *check_dcclist_hand(char *handle)
{
  int i;

  for (i = 0; i < dcc_total; i++)
    if (!egg_strcasecmp(dcc[i].nick, handle))
      return dcc[i].user;
  return NULL;
}

struct userrec *get_user_by_handle(struct userrec *bu, char *handle)
{
  struct userrec *u = NULL, *ret = NULL;

  if (!handle)
    return NULL;
  /* FIXME: This should be done outside of this function. */
  rmspace(handle);
  if (!handle[0] || (handle[0] == '*'))
    return NULL;
  if (bu == userlist) {
    if (lastuser && !egg_strcasecmp(lastuser->handle, handle)) {
      cache_hit++;
      return lastuser;
    }
    ret = check_dcclist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    ret = check_chanlist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    cache_miss++;
  }
  for (u = bu; u; u = u->next)
    if (!egg_strcasecmp(u->handle, handle)) {
      if (bu == userlist)
	lastuser = u;
      return u;
    }
  return NULL;
}

/* Fix capitalization, etc
 */
void correct_handle(char *handle)
{
  struct userrec *u = NULL;

  u = get_user_by_handle(userlist, handle);
  if (u == NULL || handle == u->handle)
    return;
  strcpy(handle, u->handle);
}

/* This will be usefull in a lot of places, much more code re-use so we
 * endup with a smaller executable bot. <cybah>
 */
void clear_masks(maskrec *m)
{
  maskrec *temp = NULL;

  for (; m; m = temp) {
    temp = m->next;
    if (m->mask)
      free(m->mask);
    if (m->user)
      free(m->user);
    if (m->desc)
      free(m->desc);
    free(m);
  }
}

void clear_userlist(struct userrec *bu)
{
  struct userrec *u = NULL, *v = NULL;
  int i;

  for (u = bu; u; u = v) {
    v = u->next;
    freeuser(u);
  }
  if (userlist == bu) {
    struct chanset_t *cst = NULL;

    for (i = 0; i < dcc_total; i++)
      dcc[i].user = NULL;

    conf.bot->u = NULL;

    clear_chanlist();
    lastuser = NULL;

    while (global_ign)
      delignore(global_ign->igmask);

    clear_masks(global_bans);
    clear_masks(global_exempts);
    clear_masks(global_invites);
    global_exempts = global_invites = global_bans = NULL;

    for (cst = chanset; cst; cst = cst->next) {
      clear_masks(cst->bans);
      clear_masks(cst->exempts);
      clear_masks(cst->invites);

      cst->bans = cst->exempts = cst->invites = NULL;
    }
  }
  /* Remember to set your userlist to NULL after calling this */
}

/* Find CLOSEST host match
 * (if "*!*@*" and "*!*@*clemson.edu" both match, use the latter!)
 *
 * Checks the chanlist first, to possibly avoid needless search.
 */
struct userrec *get_user_by_host(char *host)
{
  struct userrec *u = NULL, *ret = NULL;
  struct list_type *q = NULL;
  int cnt, i;
  char host2[UHOSTLEN] = "";

  if (host == NULL)
    return NULL;
  rmspace(host);
  if (!host[0])
    return NULL;
  ret = check_chanlist(host);
  cnt = 0;
  if (ret != NULL) {
    cache_hit++;
    return ret;
  }
  cache_miss++;
  strncpyz(host2, host, sizeof host2);
  host = fixfrom(host);
  for (u = userlist; u; u = u->next) {
    q = get_user(&USERENTRY_HOSTS, u);
    for (; q; q = q->next) {
      i = wild_match(q->extra, host);
      if (i > cnt) {
	ret = u;
	cnt = i;
      }
    }
  }
  if (ret != NULL) {
    lastuser = ret;
    set_chanlist(host2, ret);
  }
  return ret;
}

/* use fixfrom() or dont? (drummer)
 */
struct userrec *get_user_by_equal_host(char *host)
{
  struct userrec *u = NULL;
  struct list_type *q = NULL;

  for (u = userlist; u; u = u->next)
    for (q = get_user(&USERENTRY_HOSTS, u); q; q = q->next)
      if (!rfc_casecmp(q->extra, host))
	return u;
  return NULL;
}

/* Try: pass_match_by_host("-",host)
 * will return 1 if no password is set for that host
 */
int u_pass_match(struct userrec *u, char *in)
{
  char *cmp = NULL, new[32] = "", pass[16] = "";

  if (!u)
    return 0;

  egg_snprintf(pass, sizeof pass, "%s", in);

  cmp = get_user(&USERENTRY_PASS, u);
  if (!cmp && (!pass[0] || (pass[0] == '-')))
    return 1;
  if (!cmp || !pass || !pass[0] || (pass[0] == '-'))
    return 0;
  if (u->flags & USER_BOT) {
    if (!strcmp(cmp, pass))
      return 1;
  } else {
    if (strlen(pass) > 15)
      pass[15] = 0;
    encrypt_pass(pass, new);
    if (!strcmp(cmp, new))
      return 1;
  }
  return 0;
}

int write_user(struct userrec *u, FILE * f, int idx)
{
  char s[181] = "";
  struct chanuserrec *ch = NULL;
  struct chanset_t *cst = NULL;
  struct user_entry *ue = NULL;
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  fr.global = u->flags;
  fr.udef_global = u->flags_udef;
  build_flags(s, &fr, NULL);
  if (lfprintf(f, "%-10s - %-24s\n", u->handle, s) == EOF)
    return 0;
  for (ch = u->chanrec; ch; ch = ch->next) {
    cst = findchan_by_dname(ch->channel);
    if (cst && ((idx < 0) || channel_shared(cst))) {
      if (idx >= 0) {
	fr.match = (FR_CHAN | FR_BOT);
	get_user_flagrec(dcc[idx].user, &fr, ch->channel);
      } else
	fr.chan = BOT_SHARE;
      if ((fr.chan & BOT_SHARE) || (1)) {
	fr.match = FR_CHAN;
	fr.chan = ch->flags;
	fr.udef_chan = ch->flags_udef;
	build_flags(s, &fr, NULL);
	if (lfprintf(f, "! %-20s %lu %-10s %s\n", ch->channel, ch->laston, s, ch->info ? ch->info : "") == EOF)
	  return 0;
      }
    }
  }
  for (ue = u->entries; ue; ue = ue->next) {
    if (ue->name) {
      struct list_type *lt = NULL;

      for (lt = ue->u.list; lt; lt = lt->next)
	if (lfprintf(f, "--%s %s\n", ue->name, lt->extra) == EOF)
	  return 0;
    } else {
      if (!ue->type->write_userfile(f, u, ue))
	return 0;
    }
  }
  return 1;
}

int sort_compare(struct userrec *a, struct userrec *b)
{
  /* Order by flags, then alphabetically
   * first bots: +h / +a / +l / other bots
   * then users: +n / +m / +o / other users
   * return true if (a > b)
   */
  if (a->flags & b->flags & USER_BOT) {
    if (bot_hublevel(a) > bot_hublevel(b))
      return 1;
    else if (bot_hublevel(a) < bot_hublevel(b))
      return 0;
    else if (bot_hublevel(a) == bot_hublevel(b) && bot_hublevel(a) == 999)
      return 0;
  } else {
    if (~a->flags & b->flags & USER_BOT)
      return 1;
    if (a->flags & ~b->flags & USER_BOT)
      return 0;
    if (~a->flags & b->flags & USER_ADMIN)
      return 1;
    if (a->flags & ~b->flags & USER_ADMIN)
      return 0;
    if (~a->flags & b->flags & USER_OWNER)
      return 1;
    if (a->flags & ~b->flags & USER_OWNER)
      return 0;
    if (~a->flags & b->flags & USER_MASTER)
      return 1;
    if (a->flags & ~b->flags & USER_MASTER)
      return 0;
    if (~a->flags & b->flags & USER_OP)
      return 1;
    if (a->flags & ~b->flags & USER_OP)
      return 0;
  }
  return (egg_strcasecmp(a->handle, b->handle) > 0);
}

void sort_userlist()
{
  int again = 1;
  struct userrec *last = NULL, *p = NULL, *c = NULL, *n = NULL;

  while ((userlist != last) && (again)) {
    p = NULL;
    c = userlist;
    n = c->next;
    again = 0;
    while (n != last) {
      if (sort_compare(c, n)) {
	again = 1;
	c->next = n->next;
	n->next = c;
	if (p == NULL)
	  userlist = n;
	else
	  p->next = n;
      }
      p = c;
      c = n;
      n = n->next;
    }
    last = c;
  }
}

/* Rewrite the entire user file. Call USERFILE hook as well, probably
 * causing the channel file to be rewritten as well.
 */
int write_userfile(int idx)
{
  FILE *f = NULL;
  char *new_userfile = NULL, s1[81] = "", backup[DIRMAX] = "";
  time_t tt;
  struct userrec *u = NULL;
  int ok;

  if (userlist == NULL)
    return 1;			/* No point in saving userfile */

  new_userfile = malloc(strlen(userfile) + 5);
  sprintf(new_userfile, "%s~new", userfile);

  f = fopen(new_userfile, "w");
  chmod(new_userfile, 0600);
  if (f == NULL) {
    putlog(LOG_MISC, "*", USERF_ERRWRITE);
    free(new_userfile);
    return 2;
  }
  if (idx >= 0)
    dprintf(idx, "Saving userfile...\n");
  if (sort_users)
    sort_userlist();
  tt = now;
  strcpy(s1, ctime(&tt));
  lfprintf(f, "#4v: %s -- %s -- written %s", ver, conf.bot->nick, s1);
  ok = 1;
  fclose(f);
  call_hook(HOOK_USERFILE);
  f = fopen(new_userfile, "a");
  putlog(LOG_DEBUG, "@", "Writing user entries.");
  for (u = userlist; u && ok; u = u->next)
    ok = write_user(u, f, idx);
  if (!ok || fflush(f)) {
    putlog(LOG_MISC, "*", "%s (%s)", USERF_ERRWRITE, strerror(ferror(f)));
    fclose(f);
    free(new_userfile);
    return 3;
  }
  fclose(f);
  putlog(LOG_DEBUG, "@", "Done writing userfile.");
  egg_snprintf(backup, sizeof backup, "%s%s~", tempdir, userfile);
  copyfile(userfile, backup);
  movefile(new_userfile, userfile);
  free(new_userfile);
  return 0;
}

int change_handle(struct userrec *u, char *newh)
{
  int i;
  char s[HANDLEN + 1] = "";

  if (!u)
    return 0;
  /* Nothing that will confuse the userfile */
  if (!newh[1] && strchr(BADHANDCHARS, newh[0]))
    return 0;
  check_bind_nkch(u->handle, newh);
  /* Yes, even send bot nick changes now: */
  if (!noshare && !(u->flags & USER_UNSHARED))
    shareout(NULL, "h %s %s\n", u->handle, newh);
  strncpyz(s, u->handle, sizeof s);
  strncpyz(u->handle, newh, sizeof u->handle);
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type != &DCC_BOT && !egg_strcasecmp(dcc[i].nick, s)) {
      strncpyz(dcc[i].nick, newh, sizeof dcc[i].nick);
      if (dcc[i].type == &DCC_CHAT && dcc[i].u.chat->channel >= 0) {
	chanout_but(-1, dcc[i].u.chat->channel,
		    "*** Handle change: %s -> %s\n", s, newh);
	if (dcc[i].u.chat->channel < GLOBAL_CHANS)
	  botnet_send_nkch(i, s);
      }
    }
  return 1;
}


struct userrec *adduser(struct userrec *bu, char *handle, char *host, char *pass, int flags)
{
  struct userrec *u = NULL, *x = NULL;
  int oldshare = noshare;

  noshare = 1;
  u = (struct userrec *) calloc(1, sizeof(struct userrec));

  /* u->next=bu; bu=u; */
  strncpyz(u->handle, handle, sizeof u->handle);
  u->next = NULL;
  u->chanrec = NULL;
  u->entries = NULL;
  if (flags != USER_DEFAULT) { /* drummer */
    u->flags = flags;
    u->flags_udef = 0;
  } else {
    u->flags = default_flags;
    u->flags_udef = default_uflags;
  }
  set_user(&USERENTRY_PASS, u, pass);
  /* Strip out commas -- they're illegal */
  if (host && host[0]) {
    char *p = NULL;

    /* About this fixfrom():
     *   We should use this fixfrom before every call of adduser()
     *   but its much easier to use here...  (drummer)
     *   Only use it if we have a host :) (dw)
     */
    host = fixfrom(host);
    p = strchr(host, ',');

    while (p != NULL) {
      *p = '?';
      p = strchr(host, ',');
    }
    set_user(&USERENTRY_HOSTS, u, host);
  } else
    set_user(&USERENTRY_HOSTS, u, "none");
  if (bu == userlist)
    clear_chanlist();
  noshare = oldshare;
  if ((!noshare) && (handle[0] != '*') && (!(flags & USER_UNSHARED)) && (bu == userlist)) {
    struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};
    char x[100] = "";

    fr.global = u->flags;
    fr.udef_global = u->flags_udef;
    build_flags(x, &fr, 0);
    shareout(NULL, "n %s %s %s %s\n", handle, host && host[0] ? host : "none", pass, x);
  }
  if (bu == NULL)
    bu = u;
  else {
    if ((bu == userlist) && (lastuser != NULL))
      x = lastuser;
    else
      x = bu;
    while (x->next != NULL)
      x = x->next;
    x->next = u;
    if (bu == userlist)
      lastuser = u;
  }
  return bu;
}

void freeuser(struct userrec *u)
{
  struct user_entry *ue = NULL, *ut = NULL;
  struct chanuserrec *ch = NULL, *z = NULL;

  if (u == NULL)
    return;
  ch = u->chanrec;
  while (ch) {
    z = ch;
    ch = ch->next;
    if (z->info != NULL)
      free(z->info);
    free(z);
  }
  u->chanrec = NULL;
  for (ue = u->entries; ue; ue = ut) {
    ut = ue->next;
    if (ue->name) {
      struct list_type *lt, *ltt;

      for (lt = ue->u.list; lt; lt = ltt) {
	ltt = lt->next;
	free(lt->extra);
	free(lt);
      }
      free(ue->name);
      free(ue);
    } else {
      ue->type->kill(ue);
    }
  }
  free(u);
}

int deluser(char *handle)
{
  struct userrec *u = userlist, *prev = NULL;
  int fnd = 0;

  while ((u != NULL) && (!fnd)) {
    if (!egg_strcasecmp(u->handle, handle))
      fnd = 1;
    else {
      prev = u;
      u = u->next;
    }
  }
  if (!fnd)
    return 0;
  if (prev == NULL)
    userlist = u->next;
  else
    prev->next = u->next;
  if (!noshare && (handle[0] != '*') && !(u->flags & USER_UNSHARED))
    shareout(NULL, "k %s\n", handle);
  for (fnd = 0; fnd < dcc_total; fnd++)
    if (dcc[fnd].user == u)
      dcc[fnd].user = 0;	/* Clear any dcc users for this entry,
				 * null is safe-ish */
  clear_chanlist();
  freeuser(u);
  lastuser = NULL;
  return 1;
}

int delhost_by_handle(char *handle, char *host)
{
  struct userrec *u = NULL;
  struct list_type *q = NULL, *qnext = NULL, *qprev = NULL;
  struct user_entry *e = NULL;
  int i = 0;

  u = get_user_by_handle(userlist, handle);
  if (!u)
    return 0;
  q = get_user(&USERENTRY_HOSTS, u);
  qprev = q;
  if (q) {
    if (!rfc_casecmp(q->extra, host)) {
      e = find_user_entry(&USERENTRY_HOSTS, u);
      e->u.extra = q->next;
      free(q->extra);
      free(q);
      i++;
      qprev = NULL;
      q = e->u.extra;
    } else
      q = q->next;
    while (q) {
      qnext = q->next;
      if (!rfc_casecmp(q->extra, host)) {
	if (qprev)
	  qprev->next = q->next;
	else if (e) {
	  e->u.extra = q->next;
	  qprev = NULL;
	}
	free(q->extra);
	free(q);
	i++;
      } else
        qprev = q;
      q = qnext;
    }
  }
  if (!qprev)
    set_user(&USERENTRY_HOSTS, u, "none");
  if (!noshare && i && !(u->flags & USER_UNSHARED))
    shareout(NULL, "-h %s %s\n", handle, host);
  clear_chanlist();
  return i;
}

void addhost_by_handle(char *handle, char *host)
{
  struct userrec *u = get_user_by_handle(userlist, handle);

  set_user(&USERENTRY_HOSTS, u, host);
  /* u will be cached, so really no overhead, even tho this looks dumb: */
  if ((!noshare) && !(u->flags & USER_UNSHARED)) {
    if (u->flags & USER_BOT)
      shareout(NULL, "+bh %s %s\n", handle, host);
    else
      shareout(NULL, "+h %s %s\n", handle, host);
  }
  clear_chanlist();
}

void touch_laston(struct userrec *u, char *where, time_t timeval)
{
  if (!u)
    return;
  if (timeval > 1) {
    struct laston_info *li = (struct laston_info *) get_user(&USERENTRY_LASTON, u);

    if (!li)
      li = calloc(1, sizeof(struct laston_info));

    else if (li->lastonplace)
      free(li->lastonplace);
    li->laston = timeval;
    if (where) {
      li->lastonplace = strdup(where);
    } else
      li->lastonplace = NULL;
    set_user(&USERENTRY_LASTON, u, li);
  } else if (timeval == 1)
    set_user(&USERENTRY_LASTON, u, 0);

}

/*  Go through all channel records and try to find a matching
 *  nick. Will return the user's user record if that is known
 *  to the bot.  (Fabian)
 *
 *  Warning: This is unreliable by concept!
 */
struct userrec *get_user_by_nick(char *nick)
{
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;

  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0] ;m = m->next) {
      if (!rfc_casecmp(nick, m->nick)) {
  	char word[512] = "";

	egg_snprintf(word, sizeof word, "%s!%s", m->nick, m->userhost);
	/* No need to check the return value ourself */
	return get_user_by_host(word);;
      }
    }
  }
  /* Sorry, no matches */
  return NULL;
}

void user_del_chan(char *dname)
{
  struct chanuserrec *ch = NULL, *och = NULL;
  struct userrec *u = NULL;

  for (u = userlist; u; u = u->next) {
    ch = u->chanrec;
    och = NULL;
    while (ch) {
      if (!rfc_casecmp(dname, ch->channel)) {
	if (och)
	  och->next = ch->next;
	else
	  u->chanrec = ch->next;

	if (ch->info)
	  free(ch->info);
	free(ch);
	break;
      }
      och = ch;
      ch = ch->next;
    }
  }
}
