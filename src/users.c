/*
 * users.c -- handles:
 *   testing and enforcing ignores
 *   adding and removing ignores
 *   listing ignores
 *   auto-linking bots
 *   sending and receiving a userfile from a bot
 *   listing users ('.whois' and '.match')
 *   reading the user file
 *
 * dprintf'ized, 9nov1995
 *
 * $Id$
 */

#include "main.h"
#include "users.h"
#include "chan.h"
#include "modules.h"
#include "tandem.h"
char natip[121] = "";
#include <netinet/in.h>
#include <arpa/inet.h>

extern struct dcc_t *dcc;
extern struct userrec *userlist, *lastuser;
extern struct chanset_t *chanset;
extern int dcc_total, noshare, egg_numver;
extern char botnetnick[];
extern Tcl_Interp *interp;
extern time_t now;

char userfile[121] = "";	/* where the user records are stored */
int ignore_time = 10;		/* how many minutes will ignores last? */

/* is this nick!user@host being ignored? */
int match_ignore(char *uhost)
{
  struct igrec *ir;

  for (ir = global_ign; ir; ir = ir->next)
    if (wild_match(ir->igmask, uhost))
      return 1;
  return 0;
}

int equals_ignore(char *uhost)
{
  struct igrec *u = global_ign;

  for (; u; u = u->next)
    if (!rfc_casecmp(u->igmask, uhost)) {
      if (u->flags & IGREC_PERM)
	return 2;
      else
	return 1;
    }
  return 0;
}

int delignore(char *ign)
{
  int i, j;
  struct igrec **u;
  struct igrec *t;
  char temp[256];


  i = 0;
  if (!strchr(ign, '!') && (j = atoi(ign))) {
    for (u = &global_ign, j--; *u && j; u = &((*u)->next), j--);
    if (*u) {
      strncpyz(temp, (*u)->igmask, sizeof temp);
      i = 1;
    }
  } else {
    /* find the matching host, if there is one */
    for (u = &global_ign; *u && !i; u = &((*u)->next))
      if (!rfc_casecmp(ign, (*u)->igmask)) {
        strncpyz(temp, ign, sizeof temp);
	i = 1;
	break;
      }
  }
  if (i) {
    if (!noshare) {
      char *mask = str_escape(temp, ':', '\\');

      if (mask) {
	shareout(NULL, STR("-i %s\n"), mask);
	nfree(mask);
      }
    }
    nfree((*u)->igmask);
    if ((*u)->msg)
      nfree((*u)->msg);
    if ((*u)->user)
      nfree((*u)->user);
    t = *u;
    *u = (*u)->next;
    nfree(t);
  }
  return i;
}

void addignore(char *ign, char *from, char *mnote, time_t expire_time)
{
  struct igrec *p = NULL, *l;

  for (l = global_ign; l; l = l->next)
    if (!rfc_casecmp(l->igmask, ign)) {
      p = l;
      break;
    }

  if (p == NULL) {
    p = user_malloc(sizeof(struct igrec));
    p->next = global_ign;
    global_ign = p;
  } else {
    nfree(p->igmask);
    nfree(p->user);
    nfree(p->msg);
  }

  p->expire = expire_time;
  p->added = now;
  p->flags = expire_time ? 0 : IGREC_PERM;
  p->igmask = user_malloc(strlen(ign) + 1);
  strcpy(p->igmask, ign);
  p->user = user_malloc(strlen(from) + 1);
  strcpy(p->user, from);
  p->msg = user_malloc(strlen(mnote) + 1);
  strcpy(p->msg, mnote);
  if (!noshare) {
    char *mask = str_escape(ign, ':', '\\');

    if (mask) {
      shareout(NULL, STR("+i %s %lu %c %s %s\n"), mask, expire_time - now,
	       (p->flags & IGREC_PERM) ? 'p' : '-', from, mnote);
      nfree(mask);
    }
  }
}

/* take host entry from ignore list and display it ignore-style */
void display_ignore(int idx, int number, struct igrec *ignore)
{
  char dates[81], s[41];

  if (ignore->added) {
    daysago(now, ignore->added, s);
    sprintf(dates, STR("Started %s"), s);
  } else
    dates[0] = 0;
  if (ignore->flags & IGREC_PERM)
    strcpy(s, STR("(perm)"));
  else {
    char s1[41];

    days(ignore->expire, now, s1);
    sprintf(s, STR("(expires %s)"), s1);
  }
  if (number >= 0)
    dprintf(idx, STR("  [%3d] %s %s\n"), number, ignore->igmask, s);
  else
    dprintf(idx, STR("IGNORE: %s %s\n"), ignore->igmask, s);
  if (ignore->msg && ignore->msg[0])
    dprintf(idx, STR("        %s: %s\n"), ignore->user, ignore->msg);
  else
    dprintf(idx, STR("        %s %s\n"), MODES_PLACEDBY, ignore->user);
  if (dates[0])
    dprintf(idx, STR("        %s\n"), dates);
}

/* list the ignores and how long they've been active */
void tell_ignores(int idx, char *match)
{
  struct igrec *u = global_ign;
  int k = 1;

  if (u == NULL) {
    dprintf(idx, STR("No ignores.\n"));
    return;
  }
  dprintf(idx, STR("%s:\n"), IGN_CURRENT);
  for (; u; u = u->next) {
    if (match[0]) {
      if (wild_match(match, u->igmask) ||
	  wild_match(match, u->msg) ||
	  wild_match(match, u->user))
	display_ignore(idx, k, u);
      k++;
    } else
      display_ignore(idx, k++, u);
  }
}

/* check for expired timed-ignores */
void check_expired_ignores()
{
  struct igrec **u = &global_ign;

  if (!*u)
    return;
  while (*u) {
    if (!((*u)->flags & IGREC_PERM) && (now >= (*u)->expire)) {
      putlog(LOG_MISC, "*", STR("%s %s (%s)"), IGN_NOLONGER, (*u)->igmask,
	     MISC_EXPIRED);
      delignore((*u)->igmask);
    } else {
      u = &((*u)->next);
    }
  }
}

/*        Channel mask loaded from user file. This function is
 *      add(ban|invite|exempt)_fully merged into one. <cybah>
 */
static void addmask_fully(struct chanset_t *chan, maskrec **m, maskrec **global,
                         char *mask, char *from,
			 char *note, time_t expire_time, int flags,
			 time_t added, time_t last)
{
  maskrec *p = user_malloc(sizeof(maskrec));
  maskrec **u = (chan) ? m : global;

  p->next = *u;
  *u = p;
  p->expire = expire_time;
  p->added = added;
  p->lastactive = last;
  p->flags = flags;
  p->mask = user_malloc(strlen(mask) + 1);
  strcpy(p->mask, mask);
  p->user = user_malloc(strlen(from) + 1);
  strcpy(p->user, from);
  p->desc = user_malloc(strlen(note) + 1);
  strcpy(p->desc, note);
}

static void restore_chanban(struct chanset_t *chan, char *host)
{
  char *expi, *add, *last, *user, *desc;
  int flags = 0;

  expi = strchr_unescape(host, ':', '\\');
  if (expi) {
    if (*expi == '+') {
      flags |= MASKREC_PERM;
      expi++;
    }
    add = strchr(expi, ':');
    if (add) {
      if (add[-1] == '*') {
	flags |= MASKREC_STICKY;
	add[-1] = 0;
      } else
	*add = 0;
      add++;
      if (*add == '+') {
	last = strchr(add, ':');
	if (last) {
	  *last = 0;
	  last++;
	  user = strchr(last, ':');
	  if (user) {
	    *user = 0;
	    user++;
	    desc = strchr(user, ':');
	    if (desc) {
	      *desc = 0;
	      desc++;
	      addmask_fully(chan, &chan->bans, &global_bans, host, user,
			    desc, atoi(expi), flags, atoi(add), atoi(last));
	      return;
	    }
	  }
	}
      } else {
	desc = strchr(add, ':');
	if (desc) {
	  *desc = 0;
	  desc++;
	  addmask_fully(chan, &chan->bans, &global_bans, host, add, desc,
			atoi(expi), flags, now, 0);
	  return;
	}
      }
    }
  }
  putlog(LOG_MISC, "*", STR("*** Malformed banline for %s."),
	 chan ? chan->dname : STR("global_bans"));
}

static void restore_chanexempt(struct chanset_t *chan, char *host)
{
  char *expi, *add, *last, *user, *desc;
  int flags = 0;

  expi = strchr_unescape(host, ':', '\\');
  if (expi) {
      if (*expi == '+') {
	flags |= MASKREC_PERM;
	expi++;
      }
    add = strchr(expi, ':');
    if (add) {
      if (add[-1] == '*') {
	flags |= MASKREC_STICKY;
	add[-1] = 0;
      } else
	*add = 0;
      add++;
      if (*add == '+') {
	last = strchr(add, ':');
	if (last) {
	  *last = 0;
	  last++;
	  user = strchr(last, ':');
	  if (user) {
	    *user = 0;
	    user++;
	    desc = strchr(user, ':');
	    if (desc) {
	      *desc = 0;
	      desc++;
	      addmask_fully(chan, &chan->exempts, &global_exempts, host, user,
			    desc, atoi(expi), flags, atoi(add), atoi(last));
	      return;
	    }
	  }
	}
      } else {
	desc = strchr(add, ':');
	if (desc) {
	  *desc = 0;
	  desc++;
	  addmask_fully(chan, &chan->exempts, &global_exempts, host, add,
			desc, atoi(expi), flags, now, 0);
	  return;
	}
      }
    }
  }
  putlog(LOG_MISC, "*", STR("*** Malformed exemptline for %s."),
	 chan ? chan->dname : STR("global_exempts"));
}

static void restore_chaninvite(struct chanset_t *chan, char *host)
{
  char *expi, *add, *last, *user, *desc;
  int flags = 0;

  expi = strchr_unescape(host, ':', '\\');
  if (expi) {
    if (*expi == '+') {
      flags |= MASKREC_PERM;
      expi++;
    }
    add = strchr(expi, ':');
    if (add) {
      if (add[-1] == '*') {
	flags |= MASKREC_STICKY;
	add[-1] = 0;
      } else
	*add = 0;
      add++;
      if (*add == '+') {
	last = strchr(add, ':');
	if (last) {
	  *last = 0;
	  last++;
	  user = strchr(last, ':');
	  if (user) {
	    *user = 0;
	    user++;
	    desc = strchr(user, ':');
	    if (desc) {
	      *desc = 0;
	      desc++;
	      addmask_fully(chan, &chan->invites, &global_invites, host, user,
			    desc, atoi(expi), flags, atoi(add), atoi(last));
	      return;
	    }
	  }
	}
      } else {
	desc = strchr(add, ':');
	if (desc) {
	  *desc = 0;
	  desc++;
	  addmask_fully(chan, &chan->invites, &global_invites, host, add,
			desc, atoi(expi), flags, now, 0);
	  return;
	}
      }
    }
  }
  putlog(LOG_MISC, "*", STR("*** Malformed inviteline for %s."),
	 chan ? chan->dname : STR("global_invites"));
}

static void restore_ignore(char *host)
{
  char *expi, *user, *added, *desc;
  int flags = 0;
  struct igrec *p;

  expi = strchr_unescape(host, ':', '\\');
  if (expi) {
    if (*expi == '+') {
      flags |= IGREC_PERM;
      expi++;
    }
    user = strchr(expi, ':');
    if (user) {
      *user = 0;
      user++;
      added = strchr(user, ':');
      if (added) {
	*added = 0;
	added++;
	desc = strchr(added, ':');
	if (desc) {
	  *desc = 0;
	  desc++;
	} else
	  desc = NULL;
      } else {
	added = "0";
	desc = NULL;
      }
      p = user_malloc(sizeof(struct igrec));

      p->next = global_ign;
      global_ign = p;
      p->expire = atoi(expi);
      p->added = atoi(added);
      p->flags = flags;
      p->igmask = user_malloc(strlen(host) + 1);
      strcpy(p->igmask, host);
      p->user = user_malloc(strlen(user) + 1);
      strcpy(p->user, user);
      if (desc) {
	p->msg = user_malloc(strlen(desc) + 1);
	strcpy(p->msg, desc);
      } else
	p->msg = NULL;
      return;
    }
  }
  putlog(LOG_MISC, "*", STR("*** Malformed ignore line."));
}

void tell_user(int idx, struct userrec *u, int master)
{
  char s[81], s1[81];
  char format[81];
  int n;
  time_t now2;
  struct chanuserrec *ch;
  struct chanset_t *chan;
  struct user_entry *ue;
  struct laston_info *li;
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  fr.global = u->flags;
  fr.udef_global = u->flags_udef;
  build_flags(s, &fr, NULL);
  Tcl_SetVar(interp, "user", u->handle, 0);
  n = 0;
  if (Tcl_VarEval(interp, "notes ", "$user", NULL) == TCL_OK)
    n = atoi(interp->result);
  li = get_user(&USERENTRY_LASTON, u);
  if (!li || !li->laston)
    strcpy(s1, "never");
  else {
    now2 = now - li->laston;
    if (now2 > 86400)
#ifdef S_UTCTIME
      egg_strftime(s1, 7, "%d %b", gmtime(&li->laston));
#else /* !S_UTCTIME */
      egg_strftime(s1, 7, "%d %b", localtime(&li->laston));
#endif /* S_UTCTIME */
    else
#ifdef S_UTCTIME
      egg_strftime(s1, 6, "%H:%M", gmtime(&li->laston));
#else /* !S_UTCTIME */
      egg_strftime(s1, 6, "%H:%M", localtime(&li->laston));
#endif /* S_UTCTIME */
  }
  egg_snprintf(format, sizeof format, "%%-%us %%-5s%%5d %%-15s %%s (%%-10.10s)", 
                          HANDLEN);
  dprintf(idx, format, u->handle, 
	  get_user(&USERENTRY_PASS, u) ? "yes" : "no", n, s, s1,
	  (li && li->lastonplace) ? li->lastonplace : "nowhere");
  dprintf(idx, "\n");
  /* channel flags? */
  for (ch = u->chanrec; ch; ch = ch->next) {
    fr.match = FR_CHAN | FR_GLOBAL;
    chan = findchan_by_dname(ch->channel);
    get_user_flagrec(dcc[idx].user, &fr, ch->channel);
    if (!channel_private(chan) || (channel_private(chan) && (chan_op(fr) || glob_owner(fr)))) {
      if (glob_op(fr) || chan_op(fr)) {
        if (ch->laston == 0L)
  	  strcpy(s1, "never");
        else {
  	  now2 = now - (ch->laston);
	  if (now2 > 86400)
#ifdef S_UTCTIME
	    egg_strftime(s1, 7, "%d %b", gmtime(&ch->laston));
#else /* !S_UTCTIME */
	    egg_strftime(s1, 7, "%d %b", localtime(&ch->laston));
#endif /* S_UTCTIME */
	  else
#ifdef S_UTCTIME
	    egg_strftime(s1, 6, "%H:%M", gmtime(&ch->laston));
#else /* !S_UTCTIME */
	    egg_strftime(s1, 6, "%H:%M", localtime(&ch->laston));
#endif /* S_UTCTIME */
        }
        fr.match = FR_CHAN;
        fr.chan = ch->flags;
        fr.udef_chan = ch->flags_udef;
        build_flags(s, &fr, NULL);
        egg_snprintf(format, sizeof format, "%%%us  %%-18s %%-15s %%s\n", HANDLEN-9);
        dprintf(idx, format, " ", ch->channel, s, s1);
        if (ch->info != NULL)
  	  dprintf(idx, "    INFO: %s\n", ch->info);
      }
    }
  }
  /* user-defined extra fields */
  for (ue = u->entries; ue; ue = ue->next)
    if (!ue->name && ue->type->display)
      ue->type->display(idx, ue, u);
}

/* show user by ident */
void tell_user_ident(int idx, char *id, int master)
{
  char format[81];
  struct userrec *u;
  struct flag_record user = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  get_user_flagrec(dcc[idx].user, &user, NULL);

  u = get_user_by_handle(userlist, id);
  if (u == NULL)
    u = get_user_by_host(id);

  if (u == NULL || (u && !whois_access(dcc[idx].user, u))) {
    dprintf(idx, "%s.\n", USERF_NOMATCH);
    return;
  }
  egg_snprintf(format, sizeof format, STR("%%-%us PASS NOTES FLAGS           LAST\n"), 
                          HANDLEN);
  dprintf(idx, format, STR("HANDLE"));
  tell_user(idx, u, master);
}

/* match string:
 * wildcard to match nickname or hostmasks
 * +attr to find all with attr */
void tell_users_match(int idx, char *mtch, int start, int limit,
		      int master, char *chname)
{
  char format[81];
  struct userrec *u;
  int fnd = 0, cnt, nomns = 0, flags = 0;
  struct list_type *q;
  struct flag_record user, pls, mns;

  dprintf(idx, STR("*** %s '%s':\n"), MISC_MATCHING, mtch);
  cnt = 0;
  egg_snprintf(format, sizeof format, STR("%%-%us PASS NOTES FLAGS           LAST\n"), 
                      HANDLEN);
  dprintf(idx, format, STR("HANDLE"));
  if (start > 1)
    dprintf(idx, STR("(%s %d)\n"), MISC_SKIPPING, start - 1);
  if (strchr("+-&|", *mtch)) {
    user.match = pls.match = FR_GLOBAL | FR_BOT | FR_CHAN;
    break_down_flags(mtch, &pls, &mns);
    mns.match = pls.match ^ (FR_AND | FR_OR);
    if (!mns.global && !mns.udef_global && !mns.chan && !mns.udef_chan &&
	!mns.bot) {
      nomns = 1;
      if (!pls.global && !pls.udef_global && !pls.chan && !pls.udef_chan &&
	  !pls.bot) {
	/* happy now BB you weenie :P */
	dprintf(idx, STR("Unknown flag specified for matching!!\n"));
	return;
      }
    }
    if (!chname || !chname[0])
      chname = dcc[idx].u.chat->con_chan;
    flags = 1;
  }
  for (u = userlist; u; u = u->next) {
    if (!whois_access(dcc[idx].user, u)) {
      continue;
    } else if (flags) {
      get_user_flagrec(u, &user, chname);
      if (flagrec_eq(&pls, &user)) {
	if (nomns || !flagrec_eq(&mns, &user)) {
	  cnt++;
	  if ((cnt <= limit) && (cnt >= start))
	    tell_user(idx, u, master);
	  if (cnt == limit + 1)
	    dprintf(idx, MISC_TRUNCATED, limit);
	}
      }
    } else if (wild_match(mtch, u->handle)) {
      cnt++;
      if ((cnt <= limit) && (cnt >= start))
	tell_user(idx, u, master);
      if (cnt == limit + 1)
	dprintf(idx, MISC_TRUNCATED, limit);
    } else {
      fnd = 0;
      for (q = get_user(&USERENTRY_HOSTS, u); q; q = q->next) {
	if ((wild_match(mtch, q->extra)) && (!fnd)) {
	  cnt++;
	  fnd = 1;
	  if ((cnt <= limit) && (cnt >= start)) {
	    tell_user(idx, u, master);
	  }
	  if (cnt == limit + 1)
	    dprintf(idx, MISC_TRUNCATED, limit);
	}
      }
    }
  }
  dprintf(idx, MISC_FOUNDMATCH, cnt, cnt == 1 ? "" : MISC_MATCH_PLURAL);
}

#ifdef HUB
void backup_userfile()
{
  char s[DIRMAX], s2[DIRMAX];

  putlog(LOG_MISC, "*", USERF_BACKUP);
  egg_snprintf(s, sizeof s, "%s.u.0", tempdir);
  egg_snprintf(s2, sizeof s2, "%s.u.1", tempdir);
  movefile(s, s2);
  copyfile(userfile, s);
}
#endif /* HUB */


/*
 * tagged lines in the user file:
 * * OLD:
 * #  (comment)
 * ;  (comment)
 * -  hostmask(s)
 * +  email
 * *  dcc directory
 * =  comment
 * :  info line
 * .  xtra (Tcl)
 * !  channel-specific
 * !! global laston
 * :: channel-specific bans
 * NEW:
 * *ban global bans
 * *ignore global ignores
 * ::#chan channel bans
 * - entries in each
 * + denotes tcl command
 * <handle> begin user entry
 * --KEY INFO - info on each
 * NEWER:
 * % exemptmask(s)
 * @ Invitemask(s)
 * *exempt global exempts
 * *Invite global Invites
 * && channel-specific exempts
 * &&#chan channel exempts
 * $$ channel-specific Invites
 * $$#chan channel Invites
 */

int readuserfile(char *file, struct userrec **ret)
{
  char *p, buf[1024], lasthand[512], *attr, *pass, *code, s1[1024], *s, cbuf[1024], *temps;
  FILE *f;
  struct userrec *bu, *u = NULL;
  struct chanset_t *cst = NULL;
  int i, line = 0;
  char ignored[512];
  struct flag_record fr;
  struct chanuserrec *cr;

  bu = (*ret);
  ignored[0] = 0;
  if (bu == userlist) {
    clear_chanlist();
    lastuser = NULL;
    global_bans = NULL;
    global_ign = NULL;
    global_exempts = NULL;
    global_invites = NULL;
  }
  lasthand[0] = 0;
  f = fopen(file, "r");
  if (f == NULL)
    return 0;
  noshare = 1;
  /* read opening comment */
  s = buf;
  fgets(cbuf, 180, f);
  temps = (char *) decrypt_string(SALT1, cbuf);
  egg_snprintf(s, 180, temps);
  nfree(temps);
  if (s[1] < '4') {
    fatal(USERF_OLDFMT, 0);
  }
  if (s[1] > '4')
    fatal(USERF_INVALID, 0);
  while (!feof(f)) {
    s = buf;
    fgets(cbuf, 1024, f);
    temps = (char *) decrypt_string(SALT1, cbuf);
    egg_snprintf(s, 1024, temps);
    nfree(temps);
    if (!feof(f)) {
      line++;
      if (s[0] != '#' && s[0] != ';' && s[0]) {
	code = newsplit(&s);
	rmspace(s);
	if (!strcmp(code, "-")) {
	  if (!lasthand[0])
	    continue;		/* Skip this entry.	*/
	  if (u) {		/* only break it down if there a real users */
	    p = strchr(s, ',');
	    while (p != NULL) {
	      splitc(s1, s, ',');
	      rmspace(s1);
	      if (s1[0])
		set_user(&USERENTRY_HOSTS, u, s1);
	      p = strchr(s, ',');
	    }
	  }
	  /* channel bans are never stacked with , */
	  if (s[0]) {
	    if (lasthand[0] && strchr(CHANMETA, lasthand[0]) != NULL)
	      restore_chanban(cst, s);
	    else if (lasthand[0] == '*') {
	      if (lasthand[1] == IGNORE_NAME[1])
		restore_ignore(s);
#ifdef S_DCCPASS
              else if (lasthand[1] == CONFIG_NAME[1]) {
                set_cmd_pass(s, 1);
              }
#endif /* S_DCCPASS */
	      else
		restore_chanban(NULL, s);
	    } else if (lasthand[0])
	      set_user(&USERENTRY_HOSTS, u, s);
	  }
	} else if (!strcmp(code, "%")) { /* exemptmasks */
	  if (!lasthand[0])
	    continue;		/* Skip this entry.	*/
	  if (s[0]) {
	    if (lasthand[0] == '#' || lasthand[0] == '+')
	      restore_chanexempt(cst,s);
	    else if (lasthand[0] == '*')
	      if (lasthand[1] == EXEMPT_NAME[1])
		restore_chanexempt(NULL, s);
	  }
	} else if (!strcmp(code, "@")) { /* Invitemasks */
	  if (!lasthand[0])
	    continue;		/* Skip this entry.	*/
	  if (s[0]) {
	    if (lasthand[0] == '#' || lasthand[0] == '+') {
	      restore_chaninvite(cst,s);
	    } else if (lasthand[0] == '*') {
	      if (lasthand[1] == INVITE_NAME[1]) {
		restore_chaninvite(NULL, s);
              } else if (lasthand[1] == CONFIG_NAME[1]) {
                userfile_cfg_line(s);
              }
            }
	  }
	} else if (!strcmp(code, "!")) {
	  /* ! #chan laston flags [info] */
	  char *chname, *st, *fl;

	  if (u) {
	    chname = newsplit(&s);
	    st = newsplit(&s);
	    fl = newsplit(&s);
	    rmspace(s);
	    fr.match = FR_CHAN;
	    break_down_flags(fl, &fr, 0);
	    if (findchan_by_dname(chname)) {
	      for (cr = u->chanrec; cr; cr = cr->next)
		if (!rfc_casecmp(cr->channel, chname))
		  break;
	      if (!cr) {
		cr = (struct chanuserrec *)
		  user_malloc(sizeof(struct chanuserrec));

		cr->next = u->chanrec;
		u->chanrec = cr;
		strncpyz(cr->channel, chname, 80);
		cr->laston = atoi(st);
		cr->flags = fr.chan;
		cr->flags_udef = fr.udef_chan;
		if (s[0]) {
		  cr->info = (char *) user_malloc(strlen(s) + 1);
		  strcpy(cr->info, s);
		} else
		  cr->info = NULL;
	      }
	    }
	  }
        } else if (!strcmp(code, "+")) {
         if (s[0] && lasthand[0] == '*' && lasthand[1] == CHANS_NAME[1]) {
          if (Tcl_Eval(interp, s) != TCL_OK) {
           putlog(LOG_MISC, "*", "Tcl error in userfile on line %d", line);
           putlog(LOG_MISC, "*", "%s", Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY));
           return 0;
          }
         }
	} else if (!strncmp(code, "::", 2)) {
	  /* channel-specific bans */
	  strcpy(lasthand, &code[2]);
	  u = NULL;
	  if (!findchan_by_dname(lasthand)) {
	    strcpy(s1, lasthand);
	    strcat(s1, " ");
	    if (strstr(ignored, s1) == NULL) {
	      strcat(ignored, lasthand);
	      strcat(ignored, " ");
	    }
	    lasthand[0] = 0;
	  } else {
	    /* Remove all bans for this channel to avoid dupes */
	    /* NOTE only remove bans for when getting a userfile
	     * from another bot & that channel is shared */
	    cst = findchan_by_dname(lasthand);
	    if ((*ret == userlist) || channel_shared(cst)) {
	      clear_masks(cst->bans);
	      cst->bans = NULL;
	    } else {
	      /* otherwise ignore any bans for this channel */
	      cst = NULL;
	      lasthand[0] = 0;
	    }
	  }
	} else if (!strncmp(code, "&&", 2)) {
	  /* channel-specific exempts */
	  strcpy(lasthand, &code[2]);
	  u = NULL;
	  if (!findchan_by_dname(lasthand)) {
	    strcpy(s1, lasthand);
	    strcat(s1, " ");
	    if (strstr(ignored, s1) == NULL) {
	      strcat(ignored, lasthand);
	      strcat(ignored, " ");
	    }
	    lasthand[0] = 0;
	  } else {
	    /* Remove all exempts for this channel to avoid dupes */
	    /* NOTE only remove exempts for when getting a userfile
	     * from another bot & that channel is shared */
	    cst = findchan_by_dname(lasthand);
	    if ((*ret == userlist) || channel_shared(cst)) {
	      clear_masks(cst->exempts);
	      cst->exempts = NULL;
	    } else {
	      /* otherwise ignore any exempts for this channel */
	      cst = NULL;
	      lasthand[0] = 0;
	    }
	  }
	} else if (!strncmp(code, "$$", 2)) {
	  /* channel-specific invites */
	  strcpy(lasthand, &code[2]);
	  u = NULL;
	  if (!findchan_by_dname(lasthand)) {
	    strcpy(s1, lasthand);
	    strcat(s1, " ");
	    if (strstr(ignored, s1) == NULL) {
	      strcat(ignored, lasthand);
	      strcat(ignored, " ");
	    }
	    lasthand[0] = 0;
	  } else {
	    /* Remove all invites for this channel to avoid dupes */
	    /* NOTE only remove invites for when getting a userfile
	     * from another bot & that channel is shared */
	    cst = findchan_by_dname(lasthand);
	    if ((*ret == userlist) || channel_shared(cst)) {
	      clear_masks(cst->invites);
              cst->invites = NULL;
	    } else {
	      /* otherwise ignore any invites for this channel */
	      cst = NULL;
	      lasthand[0] = 0;
	    }
	  }
	} else if (!strncmp(code, "--", 2)) {
	  if (u) {
	    /* new format storage */
	    struct user_entry *ue;
	    int ok = 0;

	    for (ue = u->entries; ue && !ok; ue = ue->next)
	      if (ue->name && !egg_strcasecmp(code + 2, ue->name)) {
		struct list_type *list;

		list = user_malloc(sizeof(struct list_type));

		list->next = NULL;
		list->extra = user_malloc(strlen(s) + 1);
		strcpy(list->extra, s);
		list_append((&ue->u.list), list);
		ok = 1;
	      }
	    if (!ok) {
	      ue = user_malloc(sizeof(struct user_entry));

	      ue->name = user_malloc(strlen(code + 1));
	      ue->type = NULL;
	      strcpy(ue->name, code + 2);
	      ue->u.list = user_malloc(sizeof(struct list_type));

	      ue->u.list->next = NULL;
	      ue->u.list->extra = user_malloc(strlen(s) + 1);
	      strcpy(ue->u.list->extra, s);
	      list_insert((&u->entries), ue);
	    }
	  }
	} else if (!rfc_casecmp(code, BAN_NAME)) {
	  strcpy(lasthand, code);
	  u = NULL;
	} else if (!rfc_casecmp(code, IGNORE_NAME)) {
	  strcpy(lasthand, code);
	  u = NULL;
	} else if (!rfc_casecmp(code, EXEMPT_NAME)) {
	  strcpy(lasthand, code);
	  u = NULL;
	} else if (!rfc_casecmp(code, INVITE_NAME)) {
	  strcpy(lasthand, code);
	  u = NULL;
        } else if (!rfc_casecmp(code, CHANS_NAME)) {
          strcpy(lasthand, code);
          u = NULL;
        } else if (!rfc_casecmp(code, CONFIG_NAME)) {
          strcpy(lasthand, code);
          u = NULL;  
	} else if (code[0] == '*') {
	  lasthand[0] = 0;
	  u = NULL;
	} else {
	  pass = newsplit(&s);
	  attr = newsplit(&s);
	  rmspace(s);
	  if (!attr[0] || !pass[0]) {
	    putlog(LOG_MISC, "*", "* %s line: %d!", USERF_CORRUPT, line);
	    lasthand[0] = 0;
            return 0;
	  } else {
	    u = get_user_by_handle(bu, code);
	    if (u && !(u->flags & USER_UNSHARED)) {
	      putlog(LOG_ERROR, "@", "* %s '%s'!", USERF_DUPE, code);
	      lasthand[0] = 0;
	      u = NULL;
	    } else if (u) {
	      lasthand[0] = 0;
	      u = NULL;
	    } else {
	      fr.match = FR_GLOBAL;
	      break_down_flags(attr, &fr, 0);
	      strcpy(lasthand, code);
	      cst = NULL;
	      if (strlen(code) > HANDLEN)
		code[HANDLEN] = 0;
	      if (strlen(pass) > 20) {
		putlog(LOG_MISC, "*", "* %s '%s'", USERF_BROKEPASS, code);
		strcpy(pass, "-");
	      }
	      bu = adduser(bu, code, 0, pass,
			   sanity_check(fr.global &USER_VALID));

	      u = get_user_by_handle(bu, code);
	      for (i = 0; i < dcc_total; i++)
		if (!egg_strcasecmp(code, dcc[i].nick))
		  dcc[i].user = u;
	      u->flags_udef = fr.udef_global;
	      /* if s starts with '/' it's got file info */
	    }
	  }
	}
      }
    }
  }
  fclose(f);
  (*ret) = bu;
  if (ignored[0]) {
    putlog(LOG_MISC, "*", "%s %s", USERF_IGNBANS, ignored);
  }
  putlog(LOG_MISC, "*", "Userfile loaded, unpacking...");
  for (u = bu; u; u = u->next) {
    struct user_entry *e;

    if (!(u->flags & USER_BOT) && !egg_strcasecmp (u->handle, botnetnick)) {
      putlog(LOG_MISC, "*", "(!) I have a user record, but without +b");
      /* u->flags |= USER_BOT; */
    }

    for (e = u->entries; e; e = e->next)
      if (e->name) {
	struct user_entry_type *uet = find_entry_type(e->name);

	if (uet) {
	  e->type = uet;
	  uet->unpack(u, e);
	  nfree(e->name);
	  e->name = NULL;
	}
      }
  }
  /* process the user data *now* */
#ifdef LEAF
  unlink(userfile);
#endif /* LEAF */
  noshare = 0;
  return 1;
}

/* New methodology - cycle through list 3 times
 * 1st time scan for +sh bots and link if none connected
 * 2nd time scan for +h bots
 * 3rd time scan for +a/+h bots */

void link_pref_val(struct userrec *u, char *val)
{

/* val must be HANDLEN + 4 chars minimum */
  struct bot_addr *ba;

  val[0] = 'Z';
  val[1] = 0;
  if (!u)
    return;
  if (!(u->flags & USER_BOT))
    return;
  ba = get_user(&USERENTRY_BOTADDR, u);
  if (!ba)
    return;
  if (!ba->hublevel)
    return;
  sprintf(val, STR("%02d%s"), ba->hublevel, u->handle);

}
struct userrec *next_hub(struct userrec *current, char *lowval, char *highval)
{

/*
  starting at "current" or "userlist" if NULL, find next bot with a
  link_pref_val higher than "lowval" and lower than "highval"
  If none found return bot with best overall link_pref_val
  If still not found return NULL
*/
  char thisval[NICKLEN + 4],
    bestmatchval[NICKLEN + 4] = "z",
    bestallval[NICKLEN + 4] = "z";
  struct userrec *cur = NULL,
   *bestmatch = NULL,
   *bestall = NULL;

  if (current)
    cur = current->next;
  else
    cur = userlist;
  while (cur != current) {
    if (!cur)
      cur = userlist;
    if (cur == current)
      break;
    if ((cur->flags & USER_BOT) && (strcmp(cur->handle, botnetnick))) {
      link_pref_val(cur, thisval);
      if ((strcmp(thisval, lowval) < 0) && (strcmp(thisval, highval) > 0) &&(strcmp(thisval, bestmatchval) < 0)) {
        strcpy(bestmatchval, thisval);
        bestmatch = cur;
      }
      if ((strcmp(thisval, lowval) < 0)
          && (strcmp(thisval, bestallval) < 0)) {
        strcpy(bestallval, thisval);
        bestall = cur;
      }
    }
    cur = cur->next;
  }
  if (bestmatch)
    return bestmatch;
  if (bestall)
    return bestall;
  return NULL;
}

#ifdef HUB
void autolink_cycle(char *start)
{
  struct userrec *u = NULL;
  int i;
  char bestval[HANDLEN + 4],
    curval[HANDLEN + 4],
    myval[HANDLEN + 4];

  u = get_user_by_handle(userlist, botnetnick);
  link_pref_val(u, myval);
  strcpy(bestval, myval);
  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type == &DCC_BOT_NEW)
      return;
    if (dcc[i].type == &DCC_FORK_BOT)
      return;
    if (dcc[i].type == &DCC_BOT) {
      if (dcc[i].status & (STAT_OFFEREDU | STAT_GETTINGU | STAT_SENDINGU))
        continue; //lets let the binary have it's peace.

      if (dcc[i].u.bot->numver != egg_numver)
        continue; //same thing.

      if (dcc[i].status & (STAT_SHARE | STAT_OFFERED | STAT_SENDING | STAT_GETTING)) {
	link_pref_val(dcc[i].user, curval);

	if (strcmp(myval, curval) < 0) {
	  /* I should be aggressive to this one */
	  if (dcc[i].status & STAT_AGGRESSIVE) {
	    putlog(LOG_MISC, "*", STR("Passively sharing with %s but should be aggressive"), dcc[i].user->handle);
	    putlog(LOG_DEBUG, "*", STR("My linkval: %s - %s linkval: %s"), myval, dcc[i].nick, curval);
	    botunlink(-2, dcc[i].nick, STR("Marked passive, should be aggressive"));
	    return;
	  }
	} else {
	  /* I should be passive to this one */
	  if (!(dcc[i].status & STAT_AGGRESSIVE)) {
	    putlog(LOG_MISC, "*", STR("Aggressively sharing with %s but should be passive"), dcc[i].user->handle);
	    putlog(LOG_DEBUG, "*", STR("My linkval: %s - %s linkval: %s"), myval, dcc[i].nick, curval);
	    botunlink(-2, dcc[i].nick, STR("Marked aggressive, should be passive"));
	    return;
	  }
	  if (strcmp(curval, bestval) < 0)
	    strcpy(bestval, curval);
	}
      } else {
  	  botunlink(-2, dcc[i].nick, STR("Linked but not sharing?"));
      }
    }
  }
  if (start)
    u = get_user_by_handle(userlist, start);
  else
    u = NULL;
  if (u) {
    link_pref_val(u, curval);
    if (strcmp(bestval, curval) < 0) {
      /* This shouldn't happen. Getting a failed link attempt (start!=NULL)
         while a dcc scan indicates we *are* connected to a better bot than
         the one we failed a link to.
       */
      putlog(LOG_BOTS, "*",  STR("Failed link attempt to %s but connected to %s already???"), u->handle, (char *) &bestval[3]);
      return;
    }
  } else
    strcpy(curval, "0");
  u = next_hub(u, bestval, curval);
  if ((u) && (!in_chain(u->handle)))
    botlink("", -3, u->handle);
}
#endif /* HUB */

#ifdef LEAF

typedef struct hublist_entry {
  struct hublist_entry *next;
  struct userrec *u;
} tag_hublist_entry;

int botlinkcount = 0;

void autolink_cycle(char *start)
{
  struct userrec *my_u = NULL, *u = NULL;
  struct hublist_entry *hl = NULL, *hl2 = NULL;
  struct bot_addr *my_ba;
  char uplink[HANDLEN + 1], avoidbot[HANDLEN + 1], curhub[HANDLEN + 1];
  int i, hlc;
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  /* Reset connection attempts if we ain't called due to a failed link */
  if (!start)
    botlinkcount = 0;
  my_u = get_user_by_handle(userlist, botnetnick);
  my_ba = get_user(&USERENTRY_BOTADDR, my_u);
  if (my_ba && (my_ba->uplink[0])) {
    strncpyz(uplink, my_ba->uplink, sizeof(uplink));
  } else {
    uplink[0] = 0;
  }
  curhub[0] = 0;
  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type == &DCC_BOT_NEW) || (dcc[i].type == &DCC_FORK_BOT))
      return;
    if (dcc[i].type == &DCC_BOT) {
      strcpy(curhub, dcc[i].nick);
      break;
    }
  }

  if (curhub[0]) {
    /* we are linked to a bot (hub) */
    if (uplink[0] && !strcmp(curhub, uplink))
      /* Connected to uplink, nothing more to do */
      return;

    if (start)
      /* Failed a link... let's just wait for next regular call */
      return;

    if (uplink[0]) {
      /* Trying the uplink */
      botlink("", -3, uplink);
      return;
    }
    /* we got a hub currently, and no set uplink. Stay here */
    return;
  } else {
    /* no hubs connected... pick one */
    if (!start) {
      /* Regular interval call, no previous failed link */
      if (uplink[0]) {
	/* We have a set uplink, try it */
	botlink("", -3, uplink);
	return;
      }
      /* No preferred uplink, we need a random bot */
      avoidbot[0] = 0;
    } else {
      /* We got a failed link... */
      botlinkcount++;
      if (botlinkcount >= 3)
	/* tried 3+ random hubs without success, wait for next regular interval call */
	return;
      /* We need a random bot but *not* the last we tried */
      strcpy(avoidbot, start);
    }
  }
  /* Pick a random hub, but avoid 'avoidbot' */
  hlc = 0;
  for (u = userlist; u; u = u->next) {
    get_user_flagrec(u, &fr, NULL);
    if (glob_bot(fr) && strcmp(u->handle, botnetnick) && strcmp(u->handle, avoidbot) && (bot_hublevel(u) < 999)) {
      putlog(LOG_DEBUG, "@", STR("Adding %s to hublist"), u->handle);
      hl2 = hl;
      hl = user_malloc(sizeof(struct hublist_entry));
      egg_bzero(hl, sizeof(struct hublist_entry));

      hl->next = hl2;
      hlc++;
      hl->u = u;
    }
  }
  putlog(LOG_DEBUG, "@", STR("Picking random hub from %d hubs"), hlc);
  hlc = random() % hlc;
  putlog(LOG_DEBUG, "@", STR("Picked #%d for hub"), hlc);
  while (hl) {
    if (!hlc) {
      putlog(LOG_DEBUG, "@", STR("Which is bot: %s"), hl->u->handle);
      botlink("", -3, hl->u->handle);
    }
    hlc--;
    hl2 = hl->next;
    nfree(hl);
    hl = hl2;
  }
}
#endif /* LEAF */

int whois_access(struct userrec *user, struct userrec *whois_user)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  struct flag_record whois = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  get_user_flagrec(user, &fr, NULL);
  get_user_flagrec(whois_user, &whois, NULL);

  if ((glob_master(whois) && !glob_master(fr)) ||
     (glob_owner(whois) && !glob_owner(fr)) ||
     (glob_admin(whois) && !glob_admin(fr)) ||
     (glob_bot(whois) && !glob_master(fr)))
    return 0;
  return 1;
}

