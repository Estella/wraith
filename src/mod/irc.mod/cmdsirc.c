#ifdef LEAF
/*
 * chancmds.c -- part of irc.mod
 *   handles commands directly relating to channel interaction
 *
 * $Id$
 */

/* Do we have any flags that will allow us ops on a channel?
 */
static struct chanset_t *get_channel(int idx, char *chname)
{
  struct chanset_t *chan = NULL;

  if (chname && chname[0]) {
    chan = findchan_by_dname(chname);
    if (chan)
      return chan;
    else
      dprintf(idx, "No such channel.\n");
  } else {
    chname = dcc[idx].u.chat->con_chan;
    chan = findchan_by_dname(chname);
    if (chan)
      return chan;
    else
      dprintf(idx, "Invalid console channel.\n");
  }
  return 0;
}

/* Do we have any flags that will allow us ops on a channel?
 */
static int has_op(int idx, struct chanset_t *chan)
{
  get_user_flagrec(dcc[idx].user, &user, chan->dname);
  if (private(user, chan, PRIV_OP)) {
    dprintf(idx, "No such channel.\n");
    return 0;
  }
  if (chk_op(user, chan))
    return 1;
  dprintf(idx, "You are not a channel op on %s.\n", chan->dname);
  return 0;
}


/* Finds a nick of the handle. Returns m->nick if
 * the nick was found, otherwise NULL (Sup 1Nov2000)
 */
static char *getnick(char *handle, struct chanset_t *chan)
{
  char s[UHOSTLEN] = "";
  struct userrec *u = NULL;
  register memberlist *m = NULL;

  for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
    egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
    if ((u = get_user_by_host(s)) && !egg_strcasecmp(u->handle, handle))
      return m->nick;
  }
  return NULL;
}

static void cmd_act(struct userrec *u, int idx, char *par)
{
  char *chname = NULL;
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;

  if (!par[0]) {
    dprintf(idx, "Usage: act [channel] <action>\n");
    return;
  }
  if (strchr(CHANMETA, par[0]) != NULL)
    chname = newsplit(&par);
  else
    chname = 0;
  chan = get_channel(idx, chname);
  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  m = ismember(chan, botname);
  if (!m) {
    dprintf(idx, "Cannot say to %s: I'm not on that channel.\n", chan->dname);
    return;
  }
  if ((chan->channel.mode & CHANMODER) && !me_op(chan) && !me_voice(chan)) {
    dprintf(idx, "Cannot say to %s: It is moderated.\n", chan->dname);
    return;
  }
  putlog(LOG_CMDS, "*", "#%s# (%s) act %s", dcc[idx].nick,
	 chan->dname, par);
  dprintf(DP_HELP, "PRIVMSG %s :\001ACTION %s\001\n",
	  chan->name, par);
  dprintf(idx, "Action to %s: %s\n", chan->dname, par);
}

static void cmd_msg(struct userrec *u, int idx, char *par)
{
  char *nick = NULL;

  nick = newsplit(&par);
  if (!par[0]) 
    dprintf(idx, "Usage: msg <nick> <message>\n");
  else {
    putlog(LOG_CMDS, "*", "#%s# msg %s %s", dcc[idx].nick, nick, par);
    dprintf(DP_HELP, "PRIVMSG %s :%s\n", nick, par);
    dprintf(idx, "Msg to %s: %s\n", nick, par);
  }
}

static void cmd_say(struct userrec *u, int idx, char *par)
{
  char *chname = NULL;
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;

  if (!par[0]) {
    dprintf(idx, "Usage: say [channel] <message>\n");
    return;
  }
  if (strchr(CHANMETA, par[0]) != NULL)
    chname = newsplit(&par);
  else
    chname = 0;
  chan = get_channel(idx, chname);
  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  m = ismember(chan, botname);
  if (!m) {
    dprintf(idx, "Cannot say to %s: I'm not on that channel.\n", chan->dname);
    return;
  }
  if ((chan->channel.mode & CHANMODER) && !me_op(chan) && !me_voice(chan)) {
    dprintf(idx, "Cannot say to %s: It is moderated.\n", chan->dname);
    return;
  }
  putlog(LOG_CMDS, "*", "#%s# (%s) say %s", dcc[idx].nick, chan->dname, par);
  dprintf(DP_HELP, "PRIVMSG %s :%s\n", chan->name, par);
  dprintf(idx, "Said to %s: %s\n", chan->dname, par);
}

static void cmd_kickban(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *chname = NULL, *nick = NULL, *s1 = NULL, s[UHOSTLEN] = "", bantype = 0;
  int all = 0;
  memberlist *m = NULL;

  if (!par[0]) {
    dprintf(idx, "Usage: kickban [channel|*] [-|@]<nick> [reason]\n");
    return;
  }

  if (par[0] == '*' && par[1] == ' ') {
    all = 1;
    newsplit(&par);
  } else {
    if (strchr(CHANMETA, par[0]) != NULL)
      chname = newsplit(&par);
    else
      chname = 0;
    chan = get_channel(idx, chname);
    if (!chan || !has_op(idx, chan))
      return;
  }

  putlog(LOG_CMDS, "*", "#%s# (%s) kickban %s", dcc[idx].nick, all ? "*" : chan->dname, par);

  nick = newsplit(&par);
  if ((nick[0] == '@') || (nick[0] == '-')) {
    bantype = nick[0];
    nick++;
  }
  if (match_my_nick(nick)) {
    dprintf(idx, "I'm not going to kickban myself.\n");
    return;
  }

  if (all)
    chan = chanset;
  while (chan) {

    get_user_flagrec(dcc[idx].user, &user, chan->dname);

    if (private(user, chan, PRIV_OP)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
      return;
    }

    if (!channel_active(chan)) {
      if (all) goto next;
      dprintf(idx, "I'm not on %s right now!\n", chan->dname);
      return;
    }
    if (!me_op(chan)) {
      if (all) goto next;
      dprintf(idx, "I can't help you now because I'm not a channel op"
             " on %s.\n", chan->dname);
      return;
    }


    m = ismember(chan, nick);
    if (!m) {
      if (all) goto next;
      dprintf(idx, "%s is not on %s\n", nick, chan->dname);
      return;
    }
    egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
    u = get_user_by_host(s);
    get_user_flagrec(u, &victim, chan->dname);
  
    if ((chan_master(victim) || glob_master(victim)) &&
        !(glob_owner(user) || chan_owner(user))) {
      if (all) goto next;
      dprintf(idx, "%s is a %s master.\n", nick, chan->dname);
      return;
    }
    if (glob_bot(victim) && !(glob_owner(user) || chan_owner(user))) {
      if (all) goto next;
      dprintf(idx, "%s is another channel bot!\n", nick);
      return;
    }
    if (use_exempts && (u_match_mask(global_exempts,s) ||
        u_match_mask(chan->exempts, s))) {
      dprintf(idx, "%s is permanently exempted!\n", nick);
      return;
    }
    if (m->flags & CHANOP)
      add_mode(chan, '-', 'o', m->nick);
    check_exemptlist(chan, s);
    switch (bantype) {
      case '@':
        s1 = strchr(s, '@');
        s1 -= 3;
        s1[0] = '*';
        s1[1] = '!';
        s1[2] = '*';
        break;
      case '-':
        s1 = strchr(s, '!');
        s1[1] = '*';
        s1--;
        s1[0] = '*';
        break;
      default:
        s1 = quickban(chan, m->userhost);
        break;
    }
    if (bantype == '@' || bantype == '-')
      do_mask(chan, chan->channel.ban, s1, 'b');
    if (!par[0])
      par = "requested";
    dprintf(DP_MODE, "KICK %s %s :%s%s\n", chan->name, m->nick, bankickprefix, par);
    m->flags |= SENTKICK;
    u_addban(chan, s1, dcc[idx].nick, par, now + (60 * chan->ban_time), 0);
    dprintf(idx, "Kick-banned %s on %s.\n", nick, chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else
      chan = chan->next;
  }
}

static void cmd_voice(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *nick = NULL, s[UHOSTLEN] = "";
  int all = 0;
  memberlist *m = NULL;

  nick = newsplit(&par);
  if (par[0] == '*' && !par[1]) {
    all = 1;
    newsplit(&par);
  } else {
    chan = get_channel(idx, par);
    if (!chan || !has_op(idx, chan))
      return;
  }
  if (all)
    chan = chanset;
  putlog(LOG_CMDS, "*", "#%s# (%s) voice %s", dcc[idx].nick, all ? "*" : chan->dname , nick);
  while (chan) {
    if (!nick[0] && !(nick = getnick(u->handle, chan))) {
      if (all) goto next;
      dprintf(idx, "Usage: voice <nick> [channel|*]\n");
      return;
    }
    get_user_flagrec(dcc[idx].user, &user, chan->dname);

    if (private(user, chan, PRIV_VOICE)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
      return;
    }

    if (!channel_active(chan)) {
      if (all) goto next;
      dprintf(idx, "I'm not on %s right now!\n", chan->dname);
      return;
    }
    if (!me_op(chan)) {
      if (all) goto next;

      dprintf(idx, "I can't help you now because I'm not a chan op on %s.\n", 
          chan->dname);
      return;
    }

    m = ismember(chan, nick);
    if (!m) {
      if (all) goto next;
      dprintf(idx, "%s is not on %s.\n", nick, chan->dname);
      return;
    }
    egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
    add_mode(chan, '+', 'v', nick);
    dprintf(idx, "Gave voice to %s on %s\n", nick, chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else 
      chan = chan->next;
  }
}

static void cmd_devoice(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *nick = NULL, s[UHOSTLEN] = "";
  int all = 0;
  memberlist *m = NULL;

  nick = newsplit(&par);
  if (par[0] == '*' && !par[1]) {
    all = 1;
    newsplit(&par);
  } else {
    chan = get_channel(idx, par);
    if (!chan || !has_op(idx, chan))
      return;
  }
  if (all)
    chan = chanset;
  putlog(LOG_CMDS, "*", "#%s# (%s) devoice %s", dcc[idx].nick, all ? "*" : chan->dname, nick);
  while (chan) {
  if (!nick[0] && !(nick = getnick(u->handle, chan))) {
    if (all) goto next;
    dprintf(idx, "Usage: devoice <nick> [channel|*]\n");
    return;
  }
  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  if (private(user, chan, PRIV_VOICE)) {
    if (all) goto next;
    dprintf(idx, "No such channel.\n");
    return;
  }

  if (!channel_active(chan)) {
    if (all) goto next;
    dprintf(idx, "I'm not on %s right now!\n", chan->dname);
    return;
  }
  if (!me_op(chan)) {
    if (all) goto next;
    dprintf(idx, "I can't do that right now I'm not a chan op on %s",
	    chan->dname);
    return;
  }
  m = ismember(chan, nick);
  if (!m) {
    if (all) goto next;
    dprintf(idx, "%s is not on %s.\n", nick, chan->dname);
    return;
  }
  egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
  add_mode(chan, '-', 'v', nick);
  dprintf(idx, "Devoiced %s on %s\n", nick, chan->dname);
  next:;
  if (!all)
    chan = NULL;
  else
    chan = chan->next;
  }

}

static void cmd_op(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *nick = NULL, s[UHOSTLEN] = "";
  int all = 0;
  memberlist *m = NULL;

  nick = newsplit(&par);
  if (par[0] == '*' && !par[1]) {
    all = 1;
    newsplit(&par);
  } else {
    chan = get_channel(idx, par);
    if (!chan || !has_op(idx, chan))
      return;
  }
  if (all)
    chan = chanset;
  putlog(LOG_CMDS, "*", "#%s# (%s) op %s", dcc[idx].nick, all ? "*" : chan->dname, nick);

  while (chan) {
  get_user_flagrec(dcc[idx].user, &user, chan->dname);
  if (!nick[0] && !(nick = getnick(u->handle, chan))) {
    if (all) goto next;
    dprintf(idx, "Usage: op <nick> [channel|*]\n");
    return;
  }

  if (private(user, chan, PRIV_OP)) {
    if (!all)
      dprintf(idx, "No such channel.\n");
    goto next;
  }

  if (!channel_active(chan)) {
    if (all) goto next;
    dprintf(idx, "I'm not on %s right now!\n", chan->dname);
    return;
  }
  if (!me_op(chan)) {
    if (all) goto next;
    dprintf(idx, "I can't help you now because I'm not a chan op on %s.\n",
	    chan->dname);
    return;
  }
  m = ismember(chan, nick);
  if (!m) {
    if (all) goto next;
    dprintf(idx, "%s is not on %s.\n", nick, chan->dname);
    return;
  }
  egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
  u = get_user_by_host(s);
  get_user_flagrec(u, &victim, chan->dname);
  if (chan_deop(victim) || (glob_deop(victim) && !glob_op(victim))) {
    dprintf(idx, "%s is currently being auto-deopped  on %s.\n", m->nick, chan->dname);
    if (all) goto next;
    return;
  }
  if (channel_bitch(chan) && !chk_op(victim, chan)) {
    dprintf(idx, "%s is not a registered op on %s.\n", m->nick, chan->dname);
    if (all) goto next;
    return;
  }

  if (do_op(nick, chan, 1)) {
    dprintf(idx, "Gave op to %s on %s.\n", nick, chan->dname);
    stats_add(u, 0, 1);
  }
  next:;
    if (!all)
      chan = NULL;
    else
      chan = chan->next;
  }

}

void cmd_mdop(struct userrec *u, int idx, char *par)
{
  char *p = NULL, *chname = NULL;
  int force_bots = 0,
    force_alines = 0,
    force_slines = 0,
    force_overlap = 0;
  int overlap = 0,
    bitch = 0,
    simul = 0;
  int needed_deops,
    max_deops,
    bots,
    deops,
    sdeops;
  memberlist **chanbots = NULL,
  **targets = NULL,
   *m = NULL;
  int chanbotcount = 0,
    targetcount = 0,
    tpos = 0,
    bpos = 0,
    i;
  struct chanset_t *chan = NULL;
  char work[1024] = "";
  

  putlog(LOG_CMDS, "*", "#%s# mdop %s", dcc[idx].nick, par);

  if (strchr(CHANMETA, par[0]) != NULL)
    chname = newsplit(&par);
  else
    chname = 0;
  chan = get_channel(idx, chname);
 
  if (chan) {
    get_user_flagrec(dcc[idx].user, &user, chan->dname);

    if (!shouldjoin(chan) || !channel_active(chan))  {
      dprintf(idx, "I'm not on %s\n", chan->dname);
      return;
    }
    if (channel_pending(chan)) {
      dprintf(idx, "Channel pending.\n");
      return;
    }
    if (!glob_owner(user) && !chan_owner(user)) {
      dprintf(idx, "You do not have mdop access for %s\n", chan->dname);
      return;
    }
  }
  if (!chan || !chname[0]) {
    dprintf(idx, "Usage: mdop <#channel> [bots=n] [alines=n] [slines=n] [overlap=n] [bitch] [simul]\n");
    return;
  }


  targets = malloc(chan->channel.members * sizeof(memberlist *));

  chanbots = malloc(chan->channel.members * sizeof(memberlist *));

ContextNote("!mdop!");
  for (m = chan->channel.member; m; m = m->next)
    if (m->flags & CHANOP) {
      ContextNote(m->nick);
      if (!m->user)
	targets[targetcount++] = m;
      else if (((m->user->flags & (USER_BOT | USER_OP)) == (USER_BOT | USER_OP))
	       && (strcmp(conf.bot->nick, m->user->handle))
	       && (nextbot(m->user->handle) >= 0))
	chanbots[chanbotcount++] = m;
      else if (!(m->user->flags & USER_OP))
	targets[targetcount++] = m;
    }
  if (!chanbotcount) {
    dprintf(idx, "No bots opped on %s\n", chan->name);
    free(targets);
    free(chanbots);
    return;
  }
  if (!targetcount) {
    dprintf(idx, "Noone to deop on %s\n", chan->name);
    free(targets);
    free(chanbots);
    return;
  }
  while (par && par[0]) {
    p = newsplit(&par);
    if (!strncmp(p, "bots=", 5)) {
      p += 5;
      force_bots = atoi(p);
      if ((force_bots < 1) || (force_bots > chanbotcount)) {
	dprintf(idx, "bots must be within 1-%i\n", chanbotcount);
	free(targets);
	free(chanbots);
	return;
      }
    } else if (!strncmp(p, "alines=", 7)) {
      p += 7;
      force_alines = atoi(p);
      if ((force_alines < 1) || (force_alines > 5)) {
	dprintf(idx, "alines must be within 1-5\n");
	free(targets);
	free(chanbots);
	return;
      }
    } else if (!strncmp(p, "slines=", 7)) {
      p += 7;
      force_slines = atoi(p);
      if ((force_slines < 1) || (force_slines > 6)) {
	dprintf(idx, "slines must be within 1-6\n");
	free(targets);
	free(chanbots);
	return;
      }
    } else if (!strncmp(p, "overlap=", 8)) {
      p += 8;
      force_overlap = atoi(p);
      if ((force_overlap < 1) || (force_overlap > 8)) {
	dprintf(idx, "overlap must be within 1-8\n");
	free(targets);
	free(chanbots);
	return;
      }
    } else if (!strcmp(p, "bitch")) {
      bitch = 1;
    } else if (!strcmp(p, "simul")) {
      simul = 1;
    } else {
      dprintf(idx, "Unrecognized mdop option %s\n", p);
      free(targets);
      free(chanbots);
      return;
    }
  }

  overlap = (force_overlap ? force_overlap : 2);
  needed_deops = (overlap * targetcount);
  max_deops = ((force_bots ? force_bots : chanbotcount) * (force_alines ? force_alines : 5) * 4);

  if (needed_deops > max_deops) {
    if (overlap == 1)
      dprintf(idx, "Not enough bots.\n");
    else
      dprintf(idx, "Not enough bots. Try with overlap=1\n");
    free(targets);
    free(chanbots);
    return;
  }

  /* ok it's possible... now let's figure out how */
  if (force_bots && force_alines) {
    /* not much choice... overlap should not autochange */
    bots = force_bots;
    deops = force_alines * 4;
  } else {
    if (force_bots) {
      /* calc needed deops per bot */
      bots = force_bots;
      deops = (needed_deops + (bots - 1)) / bots;
    } else if (force_alines) {
      deops = force_alines * 4;
      bots = (needed_deops + (deops - 1)) / deops;
    } else {
      deops = 12;
      bots = (needed_deops + (deops - 1)) / deops;
      if (bots > chanbotcount) {
	deops = 16;
	bots = (needed_deops + (deops - 1)) / deops;
      }
      if (bots > chanbotcount) {
	deops = 20;
	bots = (needed_deops + (deops - 1)) / deops;
      }
      if (bots > chanbotcount) {
	putlog(LOG_MISC, "*", "Totally fucked calculations in cmd_mdop. this CAN'T happen.");
	dprintf(idx, "Something's wrong... bug the coder\n");
	free(targets);
	free(chanbots);
	return;
      }
    }
  }

  if (force_slines)
    sdeops = force_slines * 4;
  else
    sdeops = 20;
  if (sdeops < deops)
    sdeops = deops;

  dprintf(idx, "Mass deop of %s\n", chan->name);
  dprintf(idx, "  %d bots used for deop\n", bots);
  dprintf(idx, "  %d assumed deops per participating bot\n", deops);
  dprintf(idx, "  %d max deops per participating bot\n", sdeops);
  dprintf(idx, "  %d assumed deops per target nick\n", overlap);

  /* now use bots/deops to distribute nicks to deop */
  while (bots) {
    if (!simul)
      sprintf(work, "dp %s", chan->name);
    else
      work[0] = 0;
    for (i = 0; i < deops; i++) {
      strcat(work, " ");
      strcat(work, targets[tpos]->nick);
      tpos++;
      if (tpos >= targetcount)
	tpos = 0;
    }
    if (sdeops > deops) {
      int atpos;

      atpos = tpos;
      for (i = 0; i < (sdeops - deops); i++) {
	strcat(work, " ");
	strcat(work, targets[atpos]->nick);
	atpos++;
	if (atpos >= targetcount)
	  atpos = 0;
      }
    }
    if (simul)
      dprintf(idx, "%s deops%s\n", chanbots[bpos]->nick, work);
    else
      botnet_send_zapf(nextbot(chanbots[bpos]->user->handle), conf.bot->nick, chanbots[bpos]->user->handle, work);
    bots--;
    bpos++;
  }
  if (bitch && !simul && chan) {
    chan->status |= CHAN_BITCH;
    do_chanset(NULL, chan, STR("+bitch"), DO_LOCAL | DO_NET);
  }
  free(targets);
  free(chanbots);
  return;
}

void mdop_request(char *botnick, char *code, char *par)
{
  char *chname = NULL, *p = NULL;
  char work[2048] = "";

  chname = newsplit(&par);
  work[0] = 0;
  while (par[0]) {
    int cnt = 0;

    strcat(work, "MODE ");
    strcat(work, chname);
    strcat(work, " -oooo");
    while ((cnt < 4) && par[0]) {
      p = newsplit(&par);
      strcat(work, " ");
      strcat(work, p);
      cnt++;
    }
    strcat(work, "\n");
  }
  tputs(serv, work, strlen(work));
}

static void cmd_deop(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *nick = NULL, s[UHOSTLEN] = "";
  int all = 0;
  memberlist *m = NULL;

  nick = newsplit(&par);
  if (par[0] == '*' && !par[1]) {
    all = 1;
    newsplit(&par);
  } else {
    chan = get_channel(idx, par);
    if (!chan || !has_op(idx, chan))
      return;

  }
  if (all)
    chan = chanset;
  putlog(LOG_CMDS, "*", "#%s# (%s) deop %s", dcc[idx].nick, all ? "*" : chan->dname, nick);

  while (chan) {

    get_user_flagrec(dcc[idx].user, &user, chan->dname);
    if (!nick[0] && !(nick = getnick(u->handle, chan))) {
      if (all) goto next;  
      dprintf(idx, "Usage: deop <nick> [channel|*]\n");
      return;
    }
    if (private(user, chan, PRIV_OP)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
    }
    if (!channel_active(chan)) {
      if (all) goto next;  
      dprintf(idx, "I'm not on %s right now!\n", chan->dname);
      return;
    }
    if (!me_op(chan)) {
      if (all) goto next;  
      dprintf(idx, "I can't help you now because I'm not a chan op on %s.\n",
  	    chan->dname);
      return;
    }
    m = ismember(chan, nick);
    if (!m) {
      if (all) goto next;  
      dprintf(idx, "%s is not on %s.\n", nick, chan->dname);
      return;
    }
    if (match_my_nick(nick)) {
      if (all) goto next;  
      dprintf(idx, "I'm not going to deop myself.\n");
      return;
    }
    egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
    u = get_user_by_host(s);
    get_user_flagrec(u, &victim, chan->dname);

    if ((chan_master(victim) || glob_master(victim)) &&
        !(chan_owner(user) || glob_owner(user))) {
      dprintf(idx, "%s is a master for %s.\n", m->nick, chan->dname);
      if (all) goto next;  
      return;
    }
    if ((chan_op(victim) || (glob_op(victim) && !chan_deop(victim))) &&
        !(chan_master(user) || glob_master(user))) {
      dprintf(idx, "%s has the op flag for %s.\n", m->nick, chan->dname);
      if (all) goto next;  
      return;
    }
    add_mode(chan, '-', 'o', nick);
    dprintf(idx, "Deopped %s on %s.\n", nick, chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else
      chan = chan->next;
  }
}

static void cmd_kick(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *chname = NULL, *nick = NULL, s[UHOSTLEN] = "";
  int all = 0;
  memberlist *m = NULL;

  if (!par[0]) {
    dprintf(idx, "Usage: kick [channel|*] <nick> [reason]\n");
    return;
  }

  if (par[0] == '*' && par[1] == ' ') {
    all = 1;
    newsplit(&par);
  } else {
    if (strchr(CHANMETA, par[0]) != NULL)
      chname = newsplit(&par);
    else
      chname = 0;
    chan = get_channel(idx, chname);
    if (!chan || !has_op(idx, chan))
      return;
  }

  putlog(LOG_CMDS, "*", "#%s# (%s) kick %s", dcc[idx].nick, all ? "*" : chan->dname, par);

  nick = newsplit(&par);
  if (!par[0])
    par = "request";
  if (match_my_nick(nick)) {
    dprintf(idx, "I'm not going to kick myself.\n");
    return;
  }
  if (all)
    chan = chanset;
  while (chan) {

    get_user_flagrec(dcc[idx].user, &user, chan->dname);

    if (private(user, chan, PRIV_OP)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
      return;
    }

    if (!channel_active(chan)) {
      if (all) goto next;
      dprintf(idx, "I'm not on %s right now!\n", chan->dname);
      return;
    }
    if (!me_op(chan)) {
      if (all) goto next;
      dprintf(idx, "I can't help you now because I'm not a channel op %s",
  	    "on %s.\n", chan->dname);
      return;
    }

    m = ismember(chan, nick);
    if (!m) {
      if (all) goto next;
      dprintf(idx, "%s is not on %s\n", nick, chan->dname);
      return;
    }
    egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
    u = get_user_by_host(s);
    get_user_flagrec(u, &victim, chan->dname);
    if ((chan_op(victim) || (glob_op(victim) && !chan_deop(victim))) &&
        !(chan_master(user) || glob_master(user))) {
      if (all) goto next;
      dprintf(idx, "%s is a legal op.\n", nick);
      return;
    }
    if ((chan_master(victim) || glob_master(victim)) &&
        !(glob_owner(user) || chan_owner(user))) {
      if (all) goto next;
      dprintf(idx, "%s is a %s master.\n", nick, chan->dname);
      return;
    }
    if (glob_bot(victim) && !(glob_owner(user) || chan_owner(user))) {
      dprintf(idx, "%s is another channel bot!\n", nick);
      return;
    }
    dprintf(DP_SERVER, "KICK %s %s :%s%s\n", chan->name, m->nick, kickprefix, par);
    m->flags |= SENTKICK;
    dprintf(idx, "Kicked %s on %s.\n", nick, chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else
      chan = chan->next;
  }
}

static void cmd_getkey(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;

  chan = get_channel(idx, par);
  if (!chan || !has_op(idx, chan))
    return;

  putlog(LOG_CMDS, "*", "#%s getkey %s", dcc[idx].nick, par);

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  if (!glob_op(user) && !chan_op(user)) {
    dprintf(idx, "You do not have access for %s\n");
    return;
  }

  if (!(channel_pending(chan) || channel_active(chan))) {
    dprintf(idx, "I'm not on %s right now.\n", chan->dname);
    return;
  }

  if (!chan->channel.key[0])
    dprintf(idx, "%s has no key set.", chan->dname);
  else
    dprintf(idx, "Key for %s is: %s", chan->dname, chan->channel.key);
  if (chan->key_prot[0])
    dprintf(idx, " (Enforcing +k %s)", chan->key_prot);
  dprintf(idx, "\n");
}

static void cmd_mop(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  int found = 0, all = 0;

  if (par[0] == '*' && !par[1]) {
    get_user_flagrec(dcc[idx].user, &user, NULL);
    if (!glob_owner(user)) {
      dprintf(idx, "You do not have access to mop '*'\n");
      return;
    }
    all = 1;
    chan = chanset;
    newsplit(&par);
  } else {
    if (par[0] && (strchr(CHANMETA, par[0]) != NULL)) {
      char *chname = newsplit(&par);
      chan = get_channel(idx, chname);
    } else
      chan = get_channel(idx, "");
  }

  if (!chan && !all)
    return;

  putlog(LOG_CMDS, "*", "#%s# (%s) mop %s", dcc[idx].nick, all ? "*" : chan->dname, par);
  while (chan) {
    memberlist *m = NULL;

    get_user_flagrec(dcc[idx].user, &user, chan->dname);
    if (private(user, chan, PRIV_OP)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
      return;
    }
    if (!chk_op(user, chan)) {
      if (all) goto next;
      dprintf(idx, "You are not a channel op on %s.\n", chan->dname);
      return;
    }
    if (!me_op(chan)) {
      if (all) goto next;
      dprintf(idx, "I am not opped on %s.\n", chan->dname);
      return;
    }
    if (channel_active(chan) && !channel_pending(chan)) {
      for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
        if (!m->user) {
          char s[256] = "";

          sprintf(s, STR("%s!%s"), m->nick, m->userhost);
          m->user = get_user_by_host(s);
        }
        get_user_flagrec(m->user, &victim, chan->dname);
        if (!chan_hasop(m) && !glob_bot(victim) && chk_op(victim, chan)) {
          found++;
          dprintf(idx, "Gave op to '%s' as '%s' on %s\n", m->user->handle, m->nick, chan->dname);
          do_op(m->nick, chan, 0);
        }
      }
    } else {
      if (!all)
        dprintf(idx, "Channel %s is not active or is pending.\n", chan->dname);
      return;
    }
    if (!found && !all)
      dprintf(idx, "No one to op on %s\n", chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else
      chan = chan->next;
  }
}


static void cmd_find(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL, **cfound = NULL;
  memberlist *m = NULL, **found = NULL;
  struct userrec *u2 = NULL;
  int fcount = 0, tr = 0;

  putlog(LOG_CMDS, "*", STR("#%s# find %s"), dcc[idx].nick, par);

  if (!par[0]) {
    dprintf(idx, "Usage: find <nick!ident@host.com> (wildcard * allowed)\n");
    return;
  }
  for (chan = chanset; chan; chan = chan->next) {

  get_user_flagrec(dcc[idx].user, &user, chan->dname);
    if (!private(user, chan, PRIV_OP)) {

      for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
        char tmp[256] = "";

        sprintf(tmp, STR("%s!%s"), m->nick, m->userhost ? m->userhost : STR("(null)"));
        if (wild_match(par, tmp)) {
          fcount++;
          if (!found) {
            found = calloc(1, sizeof(memberlist *) * 100);
            cfound = calloc(1, sizeof(struct chanset_t *) * 100);
          }
          found[fcount - 1] = m;
          cfound[fcount - 1] = chan;
          if (fcount == 100) {
            tr = 1;
            break;
          }
        }
      }
    }
    if (fcount == 100) {
      tr = 1;
      break;
    }
  }
  if (fcount) {
    char tmp[1024] = "";
    int findex, i;

    for (findex = 0; findex < fcount; findex++) {
      char check[500] = "";

      if (found[findex]) {
        sprintf(check, "%s!%s", found[findex]->nick, found[findex]->userhost);
        u2 = get_user_by_host(check);
        sprintf(tmp, STR("%s!%s %s%s%s on %s"), found[findex]->nick, found[findex]->userhost, u2 ? "(user:" : "", u2 ? u2->handle : "", u2 ? ")" : "", cfound[findex]->name);
        for (i = findex + 1; i < fcount; i++) {
          if (found[i] && (!strcmp(found[i]->nick, found[findex]->nick))) {
            sprintf(tmp + strlen(tmp), STR(", %s"), cfound[i]->name);
            found[i] = NULL;
          }
        }
        dprintf(idx, STR("%s\n"), tmp);
      }
    }
    free(found);
    free(cfound);
  } else {
    dprintf(idx, STR("No matches for %s on any channels.\n"), par);
  }
  if (tr)
    dprintf(idx, "(more than 100 matches; list truncated)\n");
  dprintf(idx, "--- Found %d matches.\n", fcount);
}

static void cmd_invite(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;
  int all = 0;
  char *nick = NULL;

  if (!par[0])
    par = dcc[idx].nick;
  nick = newsplit(&par);
  if (par[0] == '*' && !par[1]) {
    all = 1;
    newsplit(&par);
  } else {
    chan = get_channel(idx, par);
    if (!chan || !has_op(idx, chan))
      return;
  }
  if (all)
    chan = chanset;

  putlog(LOG_CMDS, "*", "#%s# (%s) invite %s", dcc[idx].nick, all ? "*" : chan->dname,  nick);

  while (chan) {

    get_user_flagrec(dcc[idx].user, &user, chan->dname);
    if (private(user, chan, PRIV_OP)) {
      if (all) goto next;
      dprintf(idx, "No such channel.\n");
    }

    if (!me_op(chan)) {
      if (all) goto next;
      if (chan->channel.mode & CHANINV) {
        dprintf(idx, "I can't help you now because I'm not a channel op on %s", chan->dname);
        return;
      }
      if (!channel_active(chan)) {
        dprintf(idx, "I'm not on %s right now!\n", chan->dname);
        return;
      }
    }
    m = ismember(chan, nick);
    if (m && !chan_issplit(m)) {
      if (all) goto next;
      dprintf(idx, "%s is already on %s!\n", nick, chan->dname);
      return;
    }
    dprintf(DP_SERVER, "INVITE %s %s\n", nick, chan->name);
    dprintf(idx, "Inviting %s to %s.\n", nick, chan->dname);
    next:;
    if (!all)
      chan = NULL;
    else
    chan = chan->next;
  }
}

#ifdef S_AUTHCMDS
static void cmd_authed(struct userrec *u, int idx, char *par)
{
  int i = 0;
  
  putlog(LOG_CMDS, "*", "#%s# authed", dcc[idx].nick);

  dprintf(idx, "Authed:\n");
  for (i = 0; i < auth_total; i++) {
   if (auth[i].authed)
     dprintf(idx, " %d. %s!%s at %li\n", i, auth[i].nick, auth[i].host, auth[i].authtime);
  }
}
#endif /* S_AUTHCMDS */

static void cmd_channel(struct userrec *u, int idx, char *par)
{
  char handle[HANDLEN + 1] = "", s[UHOSTLEN] = "", s1[UHOSTLEN] = "", atrflag = 0, chanflag[2] = "";
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;
  int maxnicklen, maxhandlen;
  char format[81] = "";

  chan = get_channel(idx, par);
  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  putlog(LOG_CMDS, "*", "#%s# (%s) channel", dcc[idx].nick, chan->dname);
  strncpyz(s, getchanmode(chan), sizeof s);
  if (channel_pending(chan)) {
    egg_snprintf(s1, sizeof s1, "%s %s", IRC_PROCESSINGCHAN, chan->dname);
  } else if (channel_active(chan)) {
    egg_snprintf(s1, sizeof s1, "%s %s", IRC_CHANNEL, chan->dname);
  } else {
    egg_snprintf(s1, sizeof s1, "%s %s", IRC_DESIRINGCHAN, chan->dname);
  }
  dprintf(idx, "%s, %d member%s, mode %s:\n", s1, chan->channel.members,
	  chan->channel.members == 1 ? "" : "s", s);
  if (chan->channel.topic)
    dprintf(idx, "%s: %s\n", IRC_CHANNELTOPIC, chan->channel.topic);
  if (channel_active(chan)) {
    /* find max nicklen and handlen */
    maxnicklen = maxhandlen = 0;
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
	if (strlen(m->nick) > maxnicklen)
	    maxnicklen = strlen(m->nick);
	if (m->user)
	    if (strlen(m->user->handle) > maxhandlen)
		maxhandlen = strlen(m->user->handle);
    }
    if (maxnicklen < 9) maxnicklen = 9;
    if (maxhandlen < 9) maxhandlen = 9;
    
    dprintf(idx, "(n = owner, m = master, o = op, d = deop, b = bot) CAP:global\n");
    egg_snprintf(format, sizeof format, " %%-%us %%-%us %%-6s %%-4s %%-5s %%s\n", 
			maxnicklen, maxhandlen);
    dprintf(idx, format, "NICKNAME", "HANDLE", " JOIN", "  HOPS", "IDLE", "USER@HOST");
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      int hops = m->hops;
      if (m->joined > 0) {
	if ((now - (m->joined)) > 86400)
#ifdef S_UTCTIME
	  egg_strftime(s, 6, "%d%b", gmtime(&(m->joined)));
#else /* !S_UTCTIME */
	  egg_strftime(s, 6, "%d%b", localtime(&(m->joined)));
#endif /* S_UTCTIME */
	else
#ifdef S_UTCTIME
	  egg_strftime(s, 6, "%H:%M", gmtime(&(m->joined)));
#else /* !S_UTCTIME */
	  egg_strftime(s, 6, "%H:%M", localtime(&(m->joined)));
#endif /* S_UTCTIME */
      } else
	strncpyz(s, " --- ", sizeof s);
      if (m->user == NULL) {
	egg_snprintf(s1, sizeof s1, "%s!%s", m->nick, m->userhost);
	m->user = get_user_by_host(s1);
      }
      if (m->user == NULL)
	strncpyz(handle, "*", sizeof handle);
       else
       	strncpyz(handle, m->user->handle, sizeof handle);
      get_user_flagrec(m->user, &user, chan->dname);
      /* Determine status char to use */
      if (glob_bot(user) && (glob_op(user)|| chan_op(user)))
        atrflag = 'B';
      else if (glob_bot(user))
        atrflag = 'b';
      else if (glob_owner(user))
        atrflag = 'N';
      else if (chan_owner(user))
        atrflag = 'n';
      else if (glob_master(user))
        atrflag = 'M';
      else if (chan_master(user))
        atrflag = 'm';
      else if (glob_deop(user))
        atrflag = 'D';
      else if (chan_deop(user))
        atrflag = 'd';
      else if (glob_op(user))
        atrflag = 'O';
      else if (chan_op(user))
        atrflag = 'o';
      else if (glob_quiet(user))
        atrflag = 'Q';
      else if (chan_quiet(user))
        atrflag = 'q';
      else if (glob_voice(user))
        atrflag = 'V';
      else if (chan_voice(user))
        atrflag = 'v';
      else if (glob_kick(user))
        atrflag = 'K';
      else if (chan_kick(user))
        atrflag = 'k';
      else if (glob_wasoptest(user))
        atrflag = 'W';
      else if (chan_wasoptest(user))
        atrflag = 'w';
      else if (glob_exempt(user))
        atrflag = 'E';
      else if (chan_exempt(user))
        atrflag = 'e';
      else
	atrflag = ' ';

      if (chan_hasop(m))
        chanflag[1] = '@';
      else if (chan_hasvoice(m))
        chanflag[1] = '+';
      else
        chanflag[1] = ' ';
      if (m->flags & OPER)
        chanflag[0] = 'O';
      else
        chanflag[0] = ' ';

      if (chan_issplit(m)) {
        egg_snprintf(format, sizeof format, 
			"%%c%%c%%-%us %%-%us   %%d %%s %%c     <- netsplit, %%lus\n", 
			maxnicklen, maxhandlen);
	dprintf(idx, format, chanflag[0],chanflag[1], m->nick, handle, s, hops, atrflag,
		now - (m->split));
      } else if (!rfc_casecmp(m->nick, botname)) {
        egg_snprintf(format, sizeof format, 
			"%%c%%c%%-%us %%-%us %%s %%c     <- it's me!\n", 
			maxnicklen, maxhandlen);
	dprintf(idx, format, chanflag[0], chanflag[1], m->nick, handle, s, atrflag);
      } else {
	/* Determine idle time */
	if (now - (m->last) > 86400)
	  egg_snprintf(s1, sizeof s1, "%2lud", ((now - (m->last)) / 86400));
	else if (now - (m->last) > 3600)
	  egg_snprintf(s1, sizeof s1, "%2luh", ((now - (m->last)) / 3600));
	else if (now - (m->last) > 180)
	  egg_snprintf(s1, sizeof s1, "%2lum", ((now - (m->last)) / 60));
	else
	  strncpyz(s1, "   ", sizeof s1);
	egg_snprintf(format, sizeof format, "%%c%%c%%-%us %%-%us %%s %%c   %%d %%s  %%s\n", 
			maxnicklen, maxhandlen);
	dprintf(idx, format, chanflag[0], chanflag[1], m->nick,	handle, s, atrflag, hops,
                     s1, m->userhost);
      }
      if (chan_fakeop(m))
	dprintf(idx, "    (%s)\n", IRC_FAKECHANOP);
      if (chan_sentop(m))
	dprintf(idx, "    (%s)\n", IRC_PENDINGOP);
      if (chan_sentdeop(m))
	dprintf(idx, "    (%s)\n", IRC_PENDINGDEOP);
      if (chan_sentkick(m))
	dprintf(idx, "    (%s)\n", IRC_PENDINGKICK);
    }
  }
  dprintf(idx, "%s\n", IRC_ENDCHANINFO);
}

static void cmd_topic(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;

  if (par[0] && (strchr(CHANMETA, par[0]) != NULL)) {
    char *chname = newsplit(&par);
    chan = get_channel(idx, chname);
  } else
    chan = get_channel(idx, "");

  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  if (!channel_active(chan)) {
    dprintf(idx, "I'm not on %s right now!\n", chan->dname);
    return;
  }
  if (!par[0]) {
    if (chan->channel.topic) {
      dprintf(idx, "The topic for %s is: %s\n", chan->dname,
	chan->channel.topic);
    } else {
      dprintf(idx, "No topic is set for %s\n", chan->dname);
    }
  } else if (channel_optopic(chan) && !me_op(chan)) {
    dprintf(idx, "I'm not a channel op on %s and the channel %s",
	  "is +t.\n", chan->dname);
  } else {
    dprintf(DP_SERVER, "TOPIC %s :%s\n", chan->name, par);
    dprintf(idx, "Changing topic...\n");
    putlog(LOG_CMDS, "*", "#%s# (%s) topic %s", dcc[idx].nick,
	    chan->dname, par);
  }
}

static void cmd_resetbans(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *chname = newsplit(&par);

  chan = get_channel(idx, chname);
  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  putlog(LOG_CMDS, "*", "#%s# (%s) resetbans", dcc[idx].nick, chan->dname);
  dprintf(idx, "Resetting bans on %s...\n", chan->dname);
  resetbans(chan);
}

static void cmd_resetexempts(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *chname = newsplit(&par);

  chan = get_channel(idx, chname);
  if (!chan || !has_op(idx, chan))
    return;

  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  putlog(LOG_CMDS, "*", "#%s# (%s) resetexempts", dcc[idx].nick, chan->dname);
  dprintf(idx, "Resetting exempts on %s...\n", chan->dname);
  resetexempts(chan);
}

static void cmd_resetinvites(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;
  char *chname = newsplit(&par);

  chan = get_channel(idx, chname);
  if (!chan || !has_op(idx, chan))
    return;
  get_user_flagrec(dcc[idx].user, &user, chan->dname);

  putlog(LOG_CMDS, "*", "#%s# (%s) resetinvites", dcc[idx].nick, chan->dname);
  dprintf(idx, "Resetting resetinvites on %s...\n", chan->dname);
  resetinvites(chan);
}

static void cmd_adduser(struct userrec *u, int idx, char *par)
{
  char *nick = NULL, *hand = NULL;
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;
  char s[UHOSTLEN] = "", s1[UHOSTLEN] = "", s2[16] = "", s3[17] = "", tmp[50] = "";
  int atr = u ? u->flags : 0;
  int statichost = 0;
  char *p1 = s1;

  putlog(LOG_CMDS, "*", "#%s# adduser %s", dcc[idx].nick, par);

  if ((!par[0]) || ((par[0] =='!') && (!par[1]))) {
    dprintf(idx, "Usage: adduser <nick> [handle]\n");
    return;
  }
  nick = newsplit(&par);

  /* This flag allows users to have static host (added by drummer, 20Apr99) */
  if (nick[0] == '!') {
    statichost = 1;
    nick++;
  }
  if (!par[0]) {
    hand = nick;
  } else {
    char *p;
    int ok = 1;

    for (p = par; *p; p++)
      if ((*p <= 32) || (*p >= 127))
	ok = 0;
    if (!ok) {
      dprintf(idx, "You can't have strange characters in a nick.\n");
      return;
    } else if (strchr("-,+*=:!.@#;$", par[0]) != NULL) {
      dprintf(idx, "You can't start a nick with '%c'.\n", par[0]);
      return;
    }
    hand = par;
  }

  for (chan = chanset; chan; chan = chan->next) {
    m = ismember(chan, nick);
    if (m)
      break;
  }
  if (!m) {
    dprintf(idx, "%s is not on any channels I monitor\n", nick);
    return;
  }
  if (strlen(hand) > HANDLEN)
    hand[HANDLEN] = 0;
  egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
  u = get_user_by_host(s);
  if (u) {
    dprintf(idx, "%s is already known as %s.\n", nick, u->handle);
    return;
  }
  u = get_user_by_handle(userlist, hand);
  if (u && (u->flags & (USER_OWNER | USER_MASTER)) &&
      !(atr & USER_OWNER) && egg_strcasecmp(dcc[idx].nick, hand)) {
    dprintf(idx, "You can't add hostmasks to the bot owner/master.\n");
    return;
  }
  if (!statichost)
    maskhost(s, s1);
  else {
    strncpyz(s1, s, sizeof s1);
    p1 = strchr(s1, '!');
    if (strchr("~^+=-", p1[1])) {
      if (strict_host)
	p1[1] = '?';
      else {
	p1[1] = '!';
	p1++;
      }
    }
    p1--;
    p1[0] = '*';
  }
  if (!u) {
    userlist = adduser(userlist, hand, p1, "-", USER_DEFAULT);
    u = get_user_by_handle(userlist, hand);
    sprintf(tmp, STR("%lu %s"), now, dcc[idx].nick);
    set_user(&USERENTRY_ADDED, u, tmp);
    make_rand_str(s2, 15);
    set_user(&USERENTRY_PASS, u, s2);

    make_rand_str(s3, 16);
    set_user(&USERENTRY_SECPASS, u, s3);

    dprintf(idx, "Added [%s]%s with no flags.\n", hand, p1);
    dprintf(idx, STR("%s's initial password set to \002%s\002\n"), hand, s2);
    dprintf(idx, STR("%s's initial secpass set to \002%s\002\n"), hand, s3);
  } else {
    dprintf(idx, "Added hostmask %s to %s.\n", p1, u->handle);
    addhost_by_handle(hand, p1);
    get_user_flagrec(u, &user, chan->dname);
    check_this_user(hand, 0, NULL);
  }

}

static void cmd_deluser(struct userrec *u, int idx, char *par)
{
  char *nick = NULL, s[UHOSTLEN] = "";
  struct chanset_t *chan = NULL;
  memberlist *m = NULL;

  if (!par[0]) {
    dprintf(idx, "Usage: deluser <nick>\n");
    return;
  }
  nick = newsplit(&par);

  for (chan = chanset; chan; chan = chan->next) {
    m = ismember(chan, nick);
    if (m)
      break;
  }
  if (!m) {
    dprintf(idx, "%s is not on any channels I monitor\n", nick);
    return;
  }
  get_user_flagrec(u, &user, chan->dname);
  egg_snprintf(s, sizeof s, "%s!%s", m->nick, m->userhost);
  u = get_user_by_host(s);
  if (!u) {
    dprintf(idx, "%s is not a valid user.\n", nick);
    return;
  }
  get_user_flagrec(u, &victim, NULL);
  if (isowner(u->handle)) {
    dprintf(idx, "You can't remove a permanent bot owner!\n");
  } else if (glob_admin(victim) && !isowner(dcc[idx].nick)) {
    dprintf(idx, "You can't remove an admin!\n");
  } else if (glob_owner(victim)) {
    dprintf(idx, "You can't remove a bot owner!\n");
  } else if (chan_owner(victim) && !glob_owner(user)) {
    dprintf(idx, "You can't remove a channel owner!\n");
  } else if (chan_master(victim) && !(glob_owner(user) || chan_owner(user))) {
    dprintf(idx, "You can't remove a channel master!\n");
  } else if (glob_bot(victim) && !glob_owner(user)) {
    dprintf(idx, "You can't remove a bot!\n");
  } else {
    char buf[HANDLEN + 1] = "";

    strncpyz(buf, u->handle, sizeof buf);
    buf[HANDLEN] = 0;
    if (deluser(u->handle)) {
      dprintf(idx, "Deleted %s.\n", buf); /* ?!?! :) */
      putlog(LOG_CMDS, "*", "#%s# deluser %s [%s]", dcc[idx].nick, nick, buf);
    } else {
      dprintf(idx, "Failed.\n");
    }
  }
}

static void cmd_reset(struct userrec *u, int idx, char *par)
{
  struct chanset_t *chan = NULL;

  if (par[0]) {
    chan = findchan_by_dname(par);

    if (chan)
      get_user_flagrec(dcc[idx].user, &user, chan->dname);
    if (!chan || private(user, chan, PRIV_OP)) {
      dprintf(idx, "%s\n", IRC_NOMONITOR);
    } else {
      get_user_flagrec(u, &user, par);
      if (!glob_master(user) && !chan_master(user)) {
	dprintf(idx, "You are not a master on %s.\n", chan->dname);
      } else if (!channel_active(chan)) {
	dprintf(idx, "I'm not on %s at the moment!\n", chan->dname);
      } else {
	putlog(LOG_CMDS, "*", "#%s# reset %s", dcc[idx].nick, par);
	dprintf(idx, "Resetting channel info for %s...\n", chan->dname);
	reset_chan_info(chan);
      }
    }
  } else if (!(u->flags & USER_MASTER)) {
    dprintf(idx, "You are not a Bot Master.\n");
  } else {
    putlog(LOG_CMDS, "*", "#%s# reset all", dcc[idx].nick);
    dprintf(idx, "Resetting channel info for all channels...\n");
    for (chan = chanset; chan; chan = chan->next) {
      if (channel_active(chan))
	reset_chan_info(chan);
    }
  }
}

static cmd_t irc_dcc[] =
{
  {"act",		"o|o",	 (Function) cmd_act,		NULL},
  {"adduser",		"m|m",	 (Function) cmd_adduser,	NULL},
#ifdef S_AUTHCMDS
  {"authed",		"n",	 (Function) cmd_authed,		NULL},
#endif /* S_AUTHCMDS */
  {"channel",		"o|o",	 (Function) cmd_channel,	NULL},
  {"deluser",		"m|m",	 (Function) cmd_deluser,	NULL},
  {"deop",		"o|o",	 (Function) cmd_deop,		NULL},
  {"devoice",		"o|o",	 (Function) cmd_devoice,	NULL},
  {"getkey",            "o|o",   (Function) cmd_getkey,         NULL},
  {"find",		"",	 (Function) cmd_find,		NULL},
  {"invite",		"o|o",	 (Function) cmd_invite,		NULL},
  {"kick",		"o|o",	 (Function) cmd_kick,		NULL},
  {"kickban",		"o|o",	 (Function) cmd_kickban,	NULL},
  {"mdop",              "n|n",	 (Function) cmd_mdop,		NULL},
  {"mop",		"n|m",	 (Function) cmd_mop,		NULL},
  {"msg",		"o",	 (Function) cmd_msg,		NULL},
  {"op",		"o|o",	 (Function) cmd_op,		NULL},
  {"reset",		"m|m",	 (Function) cmd_reset,		NULL},
  {"resetbans",		"o|o",	 (Function) cmd_resetbans,	NULL},
  {"resetexempts",	"o|o",	 (Function) cmd_resetexempts,	NULL},
  {"resetinvites",	"o|o",	 (Function) cmd_resetinvites,	NULL},
  {"say",		"o|o",	 (Function) cmd_say,		NULL},
  {"topic",		"o|o",	 (Function) cmd_topic,		NULL},
  {"voice",		"o|o",	 (Function) cmd_voice,		NULL},
  {NULL,		NULL,	 NULL,				NULL}
};

#endif /* LEAF */
