#ifdef LEAF
/*
 * mode.c -- part of irc.mod
 *   queuing and flushing mode changes made by the bot
 *   channel mode changes and the bot's reaction to them
 *   setting and getting the current wanted channel modes
 *
 * $Id$
 */

/* Reversing this mode? */
static int reversing = 0;

#  define PLUS    BIT0
#  define MINUS   BIT1
#  define CHOP    BIT2
#  define BAN     BIT3
#  define VOICE   BIT4
#  define EXEMPT  BIT5
#  define INVITE  BIT6

static struct flag_record user = { FR_GLOBAL | FR_CHAN, 0, 0, 0 };
static struct flag_record victim = { FR_GLOBAL | FR_CHAN, 0, 0, 0 };

static int
do_op(char *nick, struct chanset_t *chan, int delay, int force)
{
  memberlist *m = ismember(chan, nick);

  if (!me_op(chan) || !m || (m && !force && chan_hasop(m)))
    return 0;

  if (channel_fastop(chan) || channel_take(chan)) {
    add_mode(chan, '+', 'o', nick);
  } else {
    add_cookie(chan, nick);
  }
  return 1;
}

static void
flush_cookies(struct chanset_t *chan, int pri)
{
  char *p = NULL, out[512] = "", post[512] = "";
  size_t postsize = sizeof(post);
  unsigned int i = 0;

  p = out;
  post[0] = 0, postsize--;

  chan->cbytes = 0;

  for (i = 0; i < (modesperline - 1); i++) {
    if (chan->ccmode[i].op && postsize > strlen(chan->ccmode[i].op)) {

      *p++ = '+';
      *p++ = 'o';
      postsize -= egg_strcatn(post, chan->ccmode[i].op, sizeof(post));
      postsize -= egg_strcatn(post, " ", sizeof(post));
      free(chan->ccmode[i].op), chan->ccmode[i].op = NULL;
    }
  }

  /* remember to terminate the buffer ('out')... */
  if (out[0]) {
    *p++ = '-';
    *p++ = 'b';
  }
  *p = 0;

  if (post[0]) {
    char *cookie = NULL;
    /* remove the trailing space... */
    size_t myindex = (sizeof(post) - 1) - postsize;

    if (myindex > 0 && post[myindex - 1] == ' ')
      post[myindex - 1] = 0;

    egg_strcatn(out, " ", sizeof(out));
    egg_strcatn(out, post, sizeof(out));
    egg_strcatn(out, " ", sizeof(out));

    cookie = makecookie(chan->dname, botname);
    egg_strcatn(out, cookie, sizeof(out));
    free(cookie);
  }
  if (out[0]) {
    if (pri == QUICK) {
      char outbuf[201] = "";

      sprintf(outbuf, "MODE %s %s\n", chan->name, out);
      tputs(serv, outbuf, strlen(outbuf));
      /* dprintf(DP_MODE, "MODE %s %s\n", chan->name, out); */
    } else
      dprintf(DP_SERVER, "MODE %s %s\n", chan->name, out);
  }
}

static void
flush_mode(struct chanset_t *chan, int pri)
{
  char *p = NULL, out[512] = "", post[512] = "";
  size_t postsize = sizeof(post);
  int plus = 2;                 /* 0 = '-', 1 = '+', 2 = none */
  unsigned int i = 0;

  if (!modesperline)            /* Haven't received 005 yet :) */
    return;

  flush_cookies(chan, pri);

/* dequeue_op_deop(chan); */
  p = out;
  post[0] = 0, postsize--;

/* now does +o first.. */
  if (chan->mns[0]) {
    *p++ = '-', plus = 0;
    for (i = 0; i < strlen(chan->mns); i++)
      *p++ = chan->mns[i];
    chan->mns[0] = 0;
  }

  if (chan->pls[0]) {
    *p++ = '+', plus = 1;
    for (i = 0; i < strlen(chan->pls); i++)
      *p++ = chan->pls[i];
    chan->pls[0] = 0;
  }

  chan->bytes = 0;
  chan->compat = 0;

  /* +k or +l ? */
  if (chan->key && !chan->rmkey) {
    if (plus != 1) {
      *p++ = '+', plus = 1;
    }
    *p++ = 'k';

    postsize -= egg_strcatn(post, chan->key, sizeof(post));
    postsize -= egg_strcatn(post, " ", sizeof(post));

    free(chan->key), chan->key = NULL;
  }

  /* max +l is signed 2^32 on IRCnet at least... so makesure we've got at least
   * a 13 char buffer for '-2147483647 \0'. We'll be overwriting the existing
   * terminating null in 'post', so makesure postsize >= 12.
   */
  if (chan->limit != 0 && postsize >= 12) {
    if (plus != 1) {
      *p++ = '+', plus = 1;
    }
    *p++ = 'l';

    /* 'sizeof(post) - 1' is used because we want to overwrite the old null */
    postsize -= sprintf(&post[(sizeof(post) - 1) - postsize], "%d ", chan->limit);

    chan->limit = 0;
  }

  /* -k ? */
  if (chan->rmkey) {
    if (plus) {
      *p++ = '-', plus = 0;
    }
    *p++ = 'k';

    postsize -= egg_strcatn(post, chan->rmkey, sizeof(post));
    postsize -= egg_strcatn(post, " ", sizeof(post));

    free(chan->rmkey), chan->rmkey = NULL;
  }

  /* Do -{b,e,I} before +{b,e,I} to avoid the server ignoring overlaps */
  for (i = 0; i < modesperline; i++) {
    if ((chan->cmode[i].type & MINUS) && postsize > strlen(chan->cmode[i].op)) {
      if (plus) {
        *p++ = '-', plus = 0;
      }

      *p++ = ((chan->cmode[i].type & BAN) ? 'b' :
              ((chan->cmode[i].type & CHOP) ? 'o' :
               ((chan->cmode[i].type & EXEMPT) ? 'e' : ((chan->cmode[i].type & INVITE) ? 'I' : 'v'))));

      postsize -= egg_strcatn(post, chan->cmode[i].op, sizeof(post));
      postsize -= egg_strcatn(post, " ", sizeof(post));

      free(chan->cmode[i].op), chan->cmode[i].op = NULL;
      chan->cmode[i].type = 0;
    }
  }

  /* now do all the + modes... */
  for (i = 0; i < modesperline; i++) {
    if ((chan->cmode[i].type & PLUS) && postsize > strlen(chan->cmode[i].op)) {
      if (plus != 1) {
        *p++ = '+', plus = 1;
      }

      *p++ = ((chan->cmode[i].type & BAN) ? 'b' :
              ((chan->cmode[i].type & CHOP) ? 'o' :
               ((chan->cmode[i].type & EXEMPT) ? 'e' : ((chan->cmode[i].type & INVITE) ? 'I' : 'v'))));

      postsize -= egg_strcatn(post, chan->cmode[i].op, sizeof(post));
      postsize -= egg_strcatn(post, " ", sizeof(post));

      free(chan->cmode[i].op), chan->cmode[i].op = NULL;
      chan->cmode[i].type = 0;
    }
  }

  /* remember to terminate the buffer ('out')... */
  *p = 0;

  if (post[0]) {
    /* remove the trailing space... */
    size_t myindex = (sizeof(post) - 1) - postsize;

    if (myindex > 0 && post[myindex - 1] == ' ')
      post[myindex - 1] = 0;

    egg_strcatn(out, " ", sizeof(out));
    egg_strcatn(out, post, sizeof(out));
  }
  if (out[0]) {
    if (pri == QUICK) {
      char outbuf[201] = "";

      sprintf(outbuf, "MODE %s %s\n", chan->name, out);
      tputs(serv, outbuf, strlen(outbuf));
      /* dprintf(DP_MODE, "MODE %s %s\n", chan->name, out); */
    } else
      dprintf(DP_SERVER, "MODE %s %s\n", chan->name, out);
  }
}

/* Queue a channel mode change
 */
void
real_add_mode(struct chanset_t *chan, const char plus, const char mode, const char *op, int cookie)
{
  int type, modes, l;
  unsigned int i;
  masklist *m = NULL;
  memberlist *mx = NULL;
  char s[21] = "";

  if (!me_op(chan))
    return;

  if (mode == 'o' || mode == 'v') {
    mx = ismember(chan, op);
    if (!mx)
      return;
    if (plus == '-' && mode == 'o') {
      if (chan_sentdeop(mx) || !chan_hasop(mx))
        return;
      mx->flags |= SENTDEOP;
    }
    if (plus == '+' && mode == 'o') {
      if (chan_sentop(mx) || chan_hasop(mx))
        return;
      mx->flags |= SENTOP;
    }
    if (plus == '-' && mode == 'v') {
      if (chan_sentdevoice(mx) || !chan_hasvoice(mx))
        return;
      mx->flags |= SENTDEVOICE;
    }
    if (plus == '+' && mode == 'v') {
      if (chan_sentvoice(mx) || chan_hasvoice(mx))
        return;
      mx->flags |= SENTVOICE;
    }
  }

  if (chan->compat == 0) {
    if (mode == 'e' || mode == 'I')
      chan->compat = 2;
    else
      chan->compat = 1;
  } else if (mode == 'e' || mode == 'I') {
    if (prevent_mixing && chan->compat == 1)
      flush_mode(chan, NORMAL);
  } else if (prevent_mixing && chan->compat == 2)
    flush_mode(chan, NORMAL);

  if (mode == 'o' || mode == 'b' || mode == 'v' || mode == 'e' || mode == 'I') {
    type = (plus == '+' ? PLUS : MINUS) |
      (mode == 'o' ? CHOP : (mode == 'b' ? BAN : (mode == 'v' ? VOICE : (mode == 'e' ? EXEMPT : INVITE))));

    /*
     * FIXME: Some networks remove overlapped bans,
     *        IRCnet does not (poptix/drummer)
     *
     * Note:  On IRCnet ischanXXX() should be used, otherwise isXXXed().
     */
    if ((plus == '-' && ((mode == 'b' && !ischanban(chan, op)) ||
                         (mode == 'e' && !ischanexempt(chan, op)) ||
                         (mode == 'I' && !ischaninvite(chan, op)))) || (plus == '+' &&
                                                                        ((mode == 'b' && ischanban(chan, op))
                                                                         || (mode == 'e' &&
                                                                             ischanexempt(chan, op)) ||
                                                                         (mode == 'I' &&
                                                                          ischaninvite(chan, op)))))
      return;

    /* If there are already max_bans bans, max_exempts exemptions,
     * max_invites invitations or max_modes +b/+e/+I modes on the
     * channel, don't try to add one more.
     */
    if (plus == '+' && (mode == 'b' || mode == 'e' || mode == 'I')) {
      int bans = 0, exempts = 0, invites = 0;

      for (m = chan->channel.ban; m && m->mask[0]; m = m->next)
        bans++;
      if ((mode == 'b') && (bans >= max_bans))
        return;

      for (m = chan->channel.exempt; m && m->mask[0]; m = m->next)
        exempts++;
      if ((mode == 'e') && (exempts >= max_exempts))
        return;

      for (m = chan->channel.invite; m && m->mask[0]; m = m->next)
        invites++;
      if ((mode == 'I') && (invites >= max_invites))
        return;

      if (bans + exempts + invites >= max_modes)
        return;
    }

    /* op-type mode change */
    /* for cookie ops, use ccmode instead of cmode */
    if (cookie) {
      for (i = 0; i < (modesperline - 1); i++)
        if (chan->ccmode[i].op != NULL && !rfc_casecmp(chan->ccmode[i].op, op))
          return;               /* Already in there :- duplicate */
      l = strlen(op) + 1;
      if (chan->cbytes + l > mode_buf_len)
        flush_mode(chan, NORMAL);
      for (i = 0; i < (modesperline - 1); i++)
        if (!chan->ccmode[i].op) {
          chan->ccmode[i].op = (char *) calloc(1, l);
          chan->cbytes += l;    /* Add 1 for safety */
          strcpy(chan->ccmode[i].op, op);
          break;
        }
    } else {
      for (i = 0; i < modesperline; i++)
        if (chan->cmode[i].type == type && chan->cmode[i].op != NULL && !rfc_casecmp(chan->cmode[i].op, op))
          return;               /* Already in there :- duplicate */
      l = strlen(op) + 1;
      if (chan->bytes + l > mode_buf_len)
        flush_mode(chan, NORMAL);
      for (i = 0; i < modesperline; i++)
        if (chan->cmode[i].type == 0) {
          chan->cmode[i].type = type;
          chan->cmode[i].op = (char *) calloc(1, l);
          chan->bytes += l;     /* Add 1 for safety */
          strcpy(chan->cmode[i].op, op);
          break;
        }
    }
  }

  /* +k ? store key */
  else if (plus == '+' && mode == 'k') {
    if (chan->key)
      free(chan->key);
    chan->key = (char *) calloc(1, strlen(op) + 1);
    strcpy(chan->key, op);
  }
  /* -k ? store removed key */
  else if (plus == '-' && mode == 'k') {
    if (chan->rmkey)
      free(chan->rmkey);
    chan->rmkey = (char *) calloc(1, strlen(op) + 1);
    strcpy(chan->rmkey, op);
  }
  /* +l ? store limit */
  else if (plus == '+' && mode == 'l')
    chan->limit = atoi(op);
  else {
    /* Typical mode changes */
    if (plus == '+')
      strcpy(s, chan->pls);
    else
      strcpy(s, chan->mns);
    if (!strchr(s, mode)) {
      if (plus == '+') {
        chan->pls[strlen(chan->pls) + 1] = 0;
        chan->pls[strlen(chan->pls)] = mode;
      } else {
        chan->mns[strlen(chan->mns) + 1] = 0;
        chan->mns[strlen(chan->mns)] = mode;
      }
    }
  }
  modes = modesperline;         /* Check for full buffer. */
  for (i = 0; i < modesperline; i++)
    if (chan->cmode[i].type)
      modes--;
  if (include_lk && chan->limit)
    modes--;
  if (include_lk && chan->rmkey)
    modes--;
  if (include_lk && chan->key)
    modes--;
  if (modes < 1)
    flush_mode(chan, NORMAL);   /* Full buffer! Flush modes. */
 
  /* flush full cookie queue */
  modes = modesperline - 1;
  for (i = 0; i < (modesperline - 1); i++)
    if (chan->ccmode[i].op)
      modes--;
  if (modes < 1)
    flush_cookies(chan, NORMAL);
}

/*
 *    Mode parsing functions
 */

static void
got_key(struct chanset_t *chan, char *nick, char *from, char *key)
{
  if ((!nick[0]) && (bounce_modes))
    reversing = 1;
  if (((reversing) && !(chan->key_prot[0])) ||
      ((chan->mode_mns_prot & CHANKEY) && !(glob_master(user) || glob_bot(user) || chan_master(user)))) {
    if (strlen(key) != 0) {
      add_mode(chan, '-', 'k', key);
    } else {
      add_mode(chan, '-', 'k', "");
    }
  }
}

static void
got_op(struct chanset_t *chan, char *nick, char *from,
       char *who, struct userrec *opu, struct flag_record *opper)
{
  memberlist *m = NULL;
  char s[UHOSTLEN] = "";
  struct userrec *u;
  int check_chan = 0;
  int snm = chan->stopnethack_mode;

  m = ismember(chan, who);
  if (!m) {
    if (channel_pending(chan))
      return;
    putlog(LOG_MISC, chan->dname, CHAN_BADCHANMODE, chan->dname, who);
    dprintf(DP_MODE, "WHO %s\n", who);
    return;
  }

  /* Did *I* just get opped? */
  if (!me_op(chan) && match_my_nick(who))
    check_chan = 1;

  if (!m->user) {
    simple_sprintf(s, "%s!%s", m->nick, m->userhost);
    u = get_user_by_host(s);
  } else
    u = m->user;

  get_user_flagrec(u, &victim, chan->dname);
  /* Flags need to be set correctly right from the beginning now, so that
   * add_mode() doesn't get irritated.
   */
  m->flags |= CHANOP;
  /* Added new meaning of WASOP:
   * in mode binds it means: was he op before get (de)opped
   * (stupid IrcNet allows opped users to be opped again and
   *  opless users to be deopped)
   * script now can use [wasop nick chan] proc to check
   * if user was op or wasnt  (drummer)
   */
  m->flags &= ~SENTOP;

  if (channel_pending(chan))
    return;

  /* I'm opped, and the opper isn't me */
  if (me_op(chan) && !match_my_nick(who) &&
      /* and it isn't a server op */
      nick[0]) {
    /* Channis is +bitch, and the opper isn't a global master or a bot */
    /* deop if they are +d or it is +bitch */
    if (chk_deop(victim) || (!loading && userlist && channel_bitch(chan) && !chk_op(victim, chan))) {     /* chk_op covers +private */
/*      char outbuf[101] = ""; */

      /* if (target_priority(chan, m, 1)) */
/*        dprintf(DP_MODE, "MODE %s -o %s\n", chan->name, who); */
      add_mode(chan, '-', 'o', who);
      flush_mode(chan, QUICK);
/*      sprintf(outbuf, "MODE %s -o %s\n", chan->name, who);
      tputs(serv, outbuf, strlen(outbuf));
*/
    } else if (reversing) {
      add_mode(chan, '-', 'o', who);
    }
  } else if (reversing && !match_my_nick(who))
    add_mode(chan, '-', 'o', who);
  if (!nick[0] && me_op(chan) && !match_my_nick(who)) {
    if (chk_deop(victim)) {
      m->flags |= FAKEOP;
      add_mode(chan, '-', 'o', who);
    } else if (snm > 0 && snm < 7 && !((0 || 0 ||
                                        0) && (chan_op(victim) || (glob_op(victim) &&
                                                                   !chan_deop(victim)))) &&
               !glob_exempt(victim) && !chan_exempt(victim)) {
      if (snm == 5)
        snm = channel_bitch(chan) ? 1 : 3;
      if (snm == 6)
        snm = channel_bitch(chan) ? 4 : 2;
      if (chan_wasoptest(victim) || glob_wasoptest(victim) || snm == 2) {
        if (!chan_wasop(m)) {
          m->flags |= FAKEOP;
          add_mode(chan, '-', 'o', who);
        }
      } else if (!(chan_op(victim) || (glob_op(victim) && !chan_deop(victim)))) {
        if (snm == 1 || snm == 4 || (snm == 3 && !chan_wasop(m))) {
          add_mode(chan, '-', 'o', who);
          m->flags |= FAKEOP;
        }
      } else if (snm == 4 && !chan_wasop(m)) {
        add_mode(chan, '-', 'o', who);
        m->flags |= FAKEOP;
      }
    }
  }
  m->flags |= WASOP;
  if (check_chan) {
    /* tell other bots to set jointime to 0 and join */
    char *buf = NULL;

    buf = (char *) calloc(1, strlen(chan->dname) + 3 + 1);

    sprintf(buf, "jn %s", chan->dname);
    putallbots(buf);
    free(buf);
    recheck_channel(chan, 1);
  }
}

static void
got_deop(struct chanset_t *chan, char *nick, char *from, char *who, struct userrec *opu)
{
  memberlist *m = NULL;
  char s[UHOSTLEN] = "", s1[UHOSTLEN] = "";
  struct userrec *u = NULL;

  m = ismember(chan, who);
  if (!m) {
    if (channel_pending(chan))
      return;
    putlog(LOG_MISC, chan->dname, CHAN_BADCHANMODE, chan->dname, who);
    dprintf(DP_MODE, "WHO %s\n", who);
    return;
  }

  simple_sprintf(s, "%s!%s", m->nick, m->userhost);
  simple_sprintf(s1, "%s!%s", nick, from);
  u = get_user_by_host(s);
  get_user_flagrec(u, &victim, chan->dname);

  /* Flags need to be set correctly right from the beginning now, so that
   * add_mode() doesn't get irritated.
   */
  m->flags &= ~(CHANOP | SENTDEOP | FAKEOP);
  /* Check comments in got_op()  (drummer) */
  m->flags &= ~WASOP;

  if (channel_pending(chan))
    return;

  /* Deop'd someone on my oplist? */
  if (me_op(chan)) {
    int ok = 1;

    /* if they aren't d|d then check if they are something we should protect */
    if (!glob_deop(victim) && !chan_deop(victim)) {
      if (channel_protectops(chan) && (glob_master(victim) || chan_master(victim) ||
                                       glob_op(victim) || chan_op(victim)))
        ok = 0;
    }

    /* do we want to reop victim? */
    if ((reversing || !ok) && !match_my_nick(nick) && rfc_casecmp(who, nick) && !match_my_nick(who) &&
        /* Is the deopper NOT a master or bot? */
        !glob_master(user) && !chan_master(user) && !glob_bot(user) &&
        ((chan_op(victim) || (glob_op(victim) && !chan_deop(victim))) || !channel_bitch(chan)))
      /* Then we'll bless the victim */
      do_op(who, chan, 0, 0);
  }

  if (!nick[0])
    putlog(LOG_MODES, chan->dname, "TS resync (%s): %s deopped by %s", chan->dname, who, from);
  /* Check for mass deop */
  if (nick[0])
    detect_chan_flood(nick, from, s1, chan, FLOOD_DEOP, who);
  /* Having op hides your +v and +h  status -- so now that someone's lost ops,
   * check to see if they have +v or +h
   */
  if (!channel_take(chan) && !channel_bitch(chan) && !(m->flags & (CHANVOICE | STOPWHO))) {
    dprintf(DP_HELP, "WHO %s\n", m->nick);
    m->flags |= STOPWHO;
  }
  /* Was the bot deopped? */
  if (match_my_nick(who)) {
    /* Cancel any pending kicks and modes */
    memberlist *m2;

    for (m2 = chan->channel.member; m2 && m2->nick[0]; m2 = m2->next)
      m2->flags &= ~(SENTKICK | SENTDEOP | SENTOP | SENTVOICE | SENTDEVOICE);

    chan->channel.do_opreq = 1;
/*    request_op(chan); */
/* need: op */
    if (!nick[0])
      putlog(LOG_MODES, chan->dname, "TS resync deopped me on %s :(", chan->dname);
  }
  if (nick[0])
    maybe_revenge(chan, s1, s, REVENGE_DEOP);
}

static void
got_ban(struct chanset_t *chan, char *nick, char *from, char *who)
{
  char me[UHOSTLEN] = "", s[UHOSTLEN] = "", s1[UHOSTLEN] = "";
  memberlist *m = NULL;
  struct userrec *u = NULL;

  egg_snprintf(me, sizeof me, "%s!%s", botname, botuserhost);
  egg_snprintf(s, sizeof s, "%s!%s", nick, from);
  newban(chan, who, s);

  if (channel_pending(chan) || !me_op(chan))
    return;

  if (wild_match(who, me) && !isexempted(chan, me)) {
    add_mode(chan, '-', 'b', who);
    reversing = 1;
    return;
  }
  if (!match_my_nick(nick)) {
    if (channel_nouserbans(chan) && nick[0] && !glob_bot(user)) {
      add_mode(chan, '-', 'b', who);
      return;
    }
    /* remove bans on ops unless a master/bot set it */
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      egg_snprintf(s1, sizeof s1, "%s!%s", m->nick, m->userhost);
      if (wild_match(who, s1)) {
        u = get_user_by_host(s1);
        if (u) {
          get_user_flagrec(u, &victim, chan->dname);
          if (chk_op(victim, chan) && !chan_master(user) && !glob_master(user) &&
              !glob_bot(user) && !isexempted(chan, s1)) {
            /* if (target_priority(chan, m, 0)) */
            add_mode(chan, '-', 'b', who);
            return;
          }
        }
      }
    }
  }
  refresh_exempt(chan, who);
  /* This looks for bans added through bot and tacks on banned: if a description is found */
  if (nick[0] && channel_enforcebans(chan)) {
    register maskrec *b = NULL;
    int cycle;
    char resn[512] = "";

    /* The point of this cycle crap is to first check chan->bans then global_bans */
    for (cycle = 0; cycle < 2; cycle++) {
      for (b = cycle ? chan->bans : global_bans; b; b = b->next) {
        if (wild_match(b->mask, who)) {
          if (b->desc && b->desc[0] != '@')
            egg_snprintf(resn, sizeof resn, "%s %s", IRC_PREBANNED, b->desc);
          else
            resn[0] = 0;
        }
      }
    }
    kick_all(chan, who, resn[0] ? (const char *) resn : response(RES_BANNED), match_my_nick(nick) ? 0 : 1);
  }
  if (!nick[0] && (bounce_bans || bounce_modes) &&
      (!u_equals_mask(global_bans, who) || !u_equals_mask(chan->bans, who)))
    add_mode(chan, '-', 'b', who);
}

static void
got_unban(struct chanset_t *chan, char *nick, char *from, char *who, struct userrec *u)
{
  masklist *b = NULL, *old = NULL;

  for (b = chan->channel.ban; b->mask[0] && rfc_casecmp(b->mask, who); old = b, b = b->next) ;
  if (b->mask[0]) {
    if (old)
      old->next = b->next;
    else
      chan->channel.ban = b->next;
    free(b->mask);
    free(b->who);
    free(b);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->bans, who) || u_sticky_mask(global_bans, who)) {
    /* That's a sticky ban! No point in being
     * sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'b', who);
  }
  if ((u_equals_mask(global_bans, who) || u_equals_mask(chan->bans, who)) &&
      me_op(chan) && !channel_dynamicbans(chan)) {
    /* That's a permban! */
    if (!glob_bot(user) && !chk_op(user, chan))
      add_mode(chan, '+', 'b', who);
  }
}

static void
got_exempt(struct chanset_t *chan, char *nick, char *from, char *who)
{
  char s[UHOSTLEN] = "";

  simple_sprintf(s, "%s!%s", nick, from);
  newexempt(chan, who, s);

  if (channel_pending(chan))
    return;

  if (!match_my_nick(nick)) {   /* It's not my exemption */
    if (channel_nouserexempts(chan) && nick[0] && !glob_bot(user) && !glob_master(user) && !chan_master(user)) {
      /* No exempts made by users */
      add_mode(chan, '-', 'e', who);
      return;
    }
    if (!nick[0] && bounce_modes)
      reversing = 1;
  }
  if (reversing || (bounce_exempts && !nick[0] &&
                    (!u_equals_mask(global_exempts, who) || !u_equals_mask(chan->exempts, who))))
    add_mode(chan, '-', 'e', who);
}

static void
got_unexempt(struct chanset_t *chan, char *nick, char *from, char *who, struct userrec *u)
{
  masklist *e = chan->channel.exempt, *old = NULL;
  masklist *b = NULL;
  int match = 0;

  while (e && e->mask[0] && rfc_casecmp(e->mask, who)) {
    old = e;
    e = e->next;
  }
  if (e && e->mask[0]) {
    if (old)
      old->next = e->next;
    else
      chan->channel.exempt = e->next;
    free(e->mask);
    free(e->who);
    free(e);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->exempts, who) || u_sticky_mask(global_exempts, who)) {
    /* That's a sticky exempt! No point in being sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'e', who);
  }
  /* If exempt was removed by master then leave it else check for bans */
  if (!nick[0] && glob_bot(user) && !glob_master(user) && !chan_master(user)) {
    b = chan->channel.ban;
    while (b->mask[0] && !match) {
      if (wild_match(b->mask, who) || wild_match(who, b->mask)) {
        add_mode(chan, '+', 'e', who);
        match = 1;
      } else
        b = b->next;
    }
  }
  if ((u_equals_mask(global_exempts, who) || u_equals_mask(chan->exempts, who)) &&
      me_op(chan) && !channel_dynamicexempts(chan) && !glob_bot(user))
    add_mode(chan, '+', 'e', who);
}

static void
got_invite(struct chanset_t *chan, char *nick, char *from, char *who)
{
  char s[UHOSTLEN] = "";

  simple_sprintf(s, "%s!%s", nick, from);
  newinvite(chan, who, s);

  if (channel_pending(chan))
    return;

  if (!match_my_nick(nick)) {   /* It's not my invitation */
    if (channel_nouserinvites(chan) && nick[0] && !glob_bot(user) && !glob_master(user) && !chan_master(user)) {
      /* No exempts made by users */
      add_mode(chan, '-', 'I', who);
      return;
    }
    if ((!nick[0]) && (bounce_modes))
      reversing = 1;
  }
  if (reversing || (bounce_invites && (!nick[0]) &&
                    (!u_equals_mask(global_invites, who) || !u_equals_mask(chan->invites, who))))
    add_mode(chan, '-', 'I', who);
}

static void
got_uninvite(struct chanset_t *chan, char *nick, char *from, char *who, struct userrec *u)
{
  masklist *inv = chan->channel.invite, *old = NULL;

  while (inv->mask[0] && rfc_casecmp(inv->mask, who)) {
    old = inv;
    inv = inv->next;
  }
  if (inv->mask[0]) {
    if (old)
      old->next = inv->next;
    else
      chan->channel.invite = inv->next;
    free(inv->mask);
    free(inv->who);
    free(inv);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->invites, who) || u_sticky_mask(global_invites, who)) {
    /* That's a sticky invite! No point in being sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'I', who);
  }
  if (!nick[0] && glob_bot(user) && !glob_master(user) && !chan_master(user)
      && (chan->channel.mode & CHANINV))
    add_mode(chan, '+', 'I', who);
  if ((u_equals_mask(global_invites, who) ||
       u_equals_mask(chan->invites, who)) && me_op(chan) && !channel_dynamicinvites(chan) && !glob_bot(user))
    add_mode(chan, '+', 'I', who);
}

static int
gotmode(char *from, char *msg)
{
  char *nick = NULL, *ch = NULL, *op = NULL, *chg = NULL, s[UHOSTLEN] = "", ms2[3] = "";
  int z, isserver = 0;
  struct userrec *u = NULL;
  memberlist *m = NULL;
  struct chanset_t *chan = NULL;

  /* Usermode changes? */
  if (msg[0] && (strchr(CHANMETA, msg[0]) != NULL)) {
    ch = newsplit(&msg);
    if (match_my_nick(ch))
      return 0;
    chan = findchan(ch);
    if (!chan) {
      putlog(LOG_MISC, "*", CHAN_FORCEJOIN, ch);
      dprintf(DP_SERVER, "PART %s\n", ch);
      return 0;
    }

    /* let's pre-emptively check for mass op/deop, manual ops and cookieops */

    if (!strchr(from, '!'))
      isserver++;

    if ((channel_active(chan) || channel_pending(chan))) {
      z = strlen(msg);
      if (msg[--z] == ' ')      /* I hate cosmetic bugs :P -poptix */
        msg[z] = 0;

      /* Split up from */
      u = get_user_by_host(from);
      nick = splitnick(&from);
      if ((m = ismember(chan, nick))) {
        m->last = now;
        if (!m->user && u)
          m->user = u;
      }

      if (!isserver) {
        char **modes = NULL;
        char tmp[1024] = "", *wptr = NULL, *p = NULL, work[1024] = "", sign = '+';
        int modecnt = 0, i = 0, n = 0, ops = 0, deops = 0, bans = 0, unbans = 0, me_opped = 0;

        /* Split up the mode: #chan modes param param param param */
        strncpyz(work, msg, sizeof(work));
        wptr = work;
        p = newsplit(&wptr);
        modes = (char **) calloc(modesperline + 1, sizeof(char *));
        while (*p) {            /* +MODES PARAM PARAM PARAM ... */
          char *mp = NULL;
          if (*p == '+')
            sign = '+';
          else if (*p == '-')
            sign = '-';
          else if (strchr("oblkvIe", p[0])) {
            mp = newsplit(&wptr);       /* PARAM as noted above */
            if (strchr("ob", p[0])) {
              /* Just want o's and b's */
              modes[modecnt] = (char *) calloc(1, strlen(mp) + 4);
              sprintf(modes[modecnt], "%c%c %s", sign, p[0], mp);
              modecnt++;
              if (p[0] == 'o') {
                if (sign == '+') {
                  ops++;
                  if (match_my_nick(mp))
                    me_opped++;
                } else
                  deops++;
              }
              if (p[0] == 'b') {
                if (sign == '+')
                  bans++;
                else
                  unbans++;
              }
            }
          } else if (strchr("pstnmi", p[0])) {
          } else {
            /* hrmm... what modechar did i forget? */
            putlog(LOG_ERRORS, "*", "Forgotten modechar in irc:gotmode: %c", p[0]);
          }
          p++;
        }

        /* take ASAP */
        if (me_opped && !me_op(chan) && channel_take(chan))
          do_take(chan);

        /* Now we got modes[], chan, u, nick, and count of each relevant mode */

        /* check for mdop */
        if (me_op(chan)) {
          if (role && (!u || (u && !u->bot)) && m && !chan_sentkick(m)) {
            if (deops >= 3) {
              m->flags |= SENTKICK;
              sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, nick, kickprefix, response(RES_MASSDEOP));
              tputs(serv, tmp, strlen(tmp));
              if (u) {
                sprintf(tmp, "Mass deop on %s by %s", chan->dname, nick);
                deflag_user(u, DEFLAG_MDOP, tmp, chan);
              }
            }

            /* check for mop */
            if (ops >= 3) {
              if (channel_nomop(chan)) {
                m->flags |= SENTKICK;
                sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, nick, kickprefix, response(RES_MANUALOP));
                tputs(serv, tmp, strlen(tmp));
                if (u) {
                  sprintf(tmp, "Mass op on %s by %s", chan->dname, nick);
                  deflag_user(u, DEFLAG_MOP, tmp, chan);
                }
                enforce_bitch(chan);        /* deop quick! */
              }
            }
          }
          if (ops && u) {
            if (u->bot && !channel_fastop(chan) && !channel_take(chan)) {
              int isbadop = 0;

              /* If no unbans or the -b is not the LAST mode, it's bad. */
              if (unbans != 1 || (strncmp(modes[modecnt - 1], "-b", 2))) {
                isbadop = BC_NOCOOKIE;
              } else {
					                 /* hash!rand@time */
                isbadop = checkcookie(chan->dname, nick, &(modes[modecnt - 1][3]));
              }
              if (isbadop) {
                char trg[NICKLEN] = "";

                putlog(LOG_WARNING, "*", "%s opped in %s with bad cookie(%d): %s", nick, chan->dname, isbadop, msg);
                n = i = 0;
                switch (role) {
                  case 0:
                    break;
                  case 1:
                    /* Kick opper */
                    if (!m || !chan_sentkick(m)) {
                      sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, nick, kickprefix, response(RES_BADOP));
                      tputs(serv, tmp, strlen(tmp));
                      if (m)
                        m->flags |= SENTKICK;
                    }
                    sprintf(tmp, "%s!%s MODE %s", nick, from, msg);
                    deflag_user(u, DEFLAG_BADCOOKIE, tmp, chan);
                    break;
                  default:
                    n = role - 1;
                    i = 0;
                    while ((i < modecnt) && (n > 0)) {
                      if (modes[i] && !strncmp(modes[i], "+o", 2))
                        n--;
                      if (n)
                        i++;
                    }
                    if (!n) {
                      memberlist *mo = NULL;

                      strncpyz(trg, (char *) &modes[i][3], NICKLEN);
                      mo = ismember(chan, trg);
                      if (mo) {
                        if (!(mo->flags & CHANOP)) {
                          if (!chan_sentkick(mo)) {
                            sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, trg, kickprefix, response(RES_BADOPPED));
                            tputs(serv, tmp, strlen(tmp));
                            mo->flags |= SENTKICK;
                          }
                        }
                      }
                    }
                }

                if (isbadop == BC_NOCOOKIE)
                  putlog(LOG_WARN, "*", "Missing cookie: %s!%s MODE %s", nick, from, msg);
                else if (isbadop == BC_HASH)
                  putlog(LOG_WARN, "*", "Invalid cookie (bad hash): %s!%s MODE %s", nick, from, msg);
                else if (isbadop == BC_SLACK)
                  putlog(LOG_WARN, "*", "Invalid cookie (bad time): %s!%s MODE %s", nick, from, msg);
              } else
                putlog(LOG_DEBUG, "@", "Good op: %s", msg);
            }

            if (!channel_manop(chan) && !u->bot) {
              char trg[NICKLEN] = "";

              n = i = 0;

              switch (role) {
                case 0:
                  break;
                case 1:
                  /* Kick opper */
                  if (!m || !chan_sentkick(m)) {
                    sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, nick, kickprefix, response(RES_MANUALOP));
                    tputs(serv, tmp, strlen(tmp));
                    if (m)
                      m->flags |= SENTKICK;
                  }
                  sprintf(tmp, "%s!%s MODE %s", nick, from, msg);
                  deflag_user(u, DEFLAG_MANUALOP, tmp, chan);
                  break;
                default:
                  n = role - 1;
                  i = 0;
                  while ((i < modecnt) && (n > 0)) {
                    if (modes[i] && !strncmp(modes[i], "+o", 2))
                      n--;
                    if (n)
                      i++;
                  }
                  if (!n) {
                    memberlist *mo = NULL;

                    strncpyz(trg, (char *) &modes[i][3], NICKLEN);
                    mo = ismember(chan, trg);
                    if (mo) {
                      if (!(mo->flags & CHANOP) && !match_my_nick(trg)) {
                        if (!chan_sentkick(mo)) {
                          sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, trg, kickprefix,
                                  response(RES_MANUALOPPED));
                          tputs(serv, tmp, strlen(tmp));
                          m->flags |= SENTKICK;
                        }
                      }
                    } else {
                      sprintf(tmp, "KICK %s %s :%s%s\n", chan->name, trg, kickprefix,
                              response(RES_MANUALOPPED));
                      tputs(serv, tmp, strlen(tmp));
                    }
                  }
              }
            }
          }
        }
        for (i = 0; i < modecnt; i++)
          if (modes[i])
            free(modes[i]);
        free(modes);
      }
      /* Now do the modes again, this time throughly... */
      chg = newsplit(&msg);
      reversing = 0;

      irc_log(chan, "%s!%s sets mode: %s %s", nick, from, chg, msg);
      get_user_flagrec(u, &user, ch);

      if (channel_active(chan) && m && me_op(chan)) {
        if (chan_fakeop(m)) {
          putlog(LOG_MODES, ch, CHAN_FAKEMODE, ch);
          dprintf(DP_MODE, "KICK %s %s :%s%s\n", ch, nick, kickprefix, CHAN_FAKEMODE_KICK);
          m->flags |= SENTKICK;
          reversing = 1;
        } else if (m->nick && !isserver &&      /* HOW ABOUT IF THEY ARENT A FUCKING SERVER? */
                   !chan_hasop(m) && !channel_nodesynch(chan)) {
          putlog(LOG_MODES, ch, CHAN_DESYNCMODE, ch);
          dprintf(DP_MODE, "KICK %s %s :%s%s\n", ch, nick, kickprefix, CHAN_DESYNCMODE_KICK);
          m->flags |= SENTKICK;
          reversing = 1;
        }
      }
      ms2[0] = '+';
      ms2[2] = 0;
      while ((ms2[1] = *chg)) {
        int todo = 0;

        switch (*chg) {
          case '+':
            ms2[0] = '+';
            break;
          case '-':
            ms2[0] = '-';
            break;
          case 'i':
            todo = CHANINV;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'p':
            todo = CHANPRIV;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 's':
            todo = CHANSEC;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'm':
            todo = CHANMODER;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'c':
            todo = CHANNOCLR;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'C':
            todo = CHANNOCTCP;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'R':
            todo = CHANREGON;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'M':
            todo = CHANMODR;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'r':
            todo = CHANLONLY;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 't':
            todo = CHANTOPIC;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'n':
            todo = CHANNOMSG;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'a':
            todo = CHANANON;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'q':
            todo = CHANQUIET;
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            break;
          case 'l':
            if ((!nick[0]) && (bounce_modes))
              reversing = 1;
            if (ms2[0] == '-') {
              if (channel_active(chan)) {
                if ((reversing) && (chan->channel.maxmembers != 0)) {
                  simple_sprintf(s, "%d", chan->channel.maxmembers);
                  add_mode(chan, '+', 'l', s);
                } else if ((chan->limit_prot != 0) && !glob_master(user) && !chan_master(user)) {
                  simple_sprintf(s, "%d", chan->limit_prot);
                  add_mode(chan, '+', 'l', s);
                } else {
                  if (dolimit(chan) && (!chan_master(user) && !glob_master(user) && !glob_bot(user))) {
                    if (chan->limitraise) {
                      chan->channel.maxmembers = 0;     /* set this to 0 so a new limit is generated */
                      raise_limit(chan);
                    }
                  }
                }
              }
              chan->channel.maxmembers = 0;
            } else {
              op = newsplit(&msg);
              fixcolon(op);
              if (op == '\0')
                break;
              chan->channel.maxmembers = atoi(op);
              if (channel_pending(chan))
                break;
              if (((reversing) &&
                   !(chan->mode_pls_prot & CHANLIMIT)) ||
                  ((chan->mode_mns_prot & CHANLIMIT) && !glob_master(user) && !chan_master(user)))
                add_mode(chan, '-', 'l', "");
              if ((chan->limit_prot != chan->channel.maxmembers) && (chan->mode_pls_prot & CHANLIMIT) && (chan->limit_prot != 0) &&     /* arthur2 */
                  !glob_master(user) && !chan_master(user)) {
                simple_sprintf(s, "%d", chan->limit_prot);
                add_mode(chan, '+', 'l', s);
              }
              if (!glob_bot(user))
                if (dolimit(chan) && (!chan_master(user) && !glob_master(user)))
                  if (chan->limitraise)
                    raise_limit(chan);

            }
            break;
          case 'k':
            if (ms2[0] == '+')
              chan->channel.mode |= CHANKEY;
            else
              chan->channel.mode &= ~CHANKEY;
            op = newsplit(&msg);
            fixcolon(op);
            if (op == '\0') {
              break;
            }
            if (ms2[0] == '+') {
              my_setkey(chan, op);
              if (channel_active(chan))
                got_key(chan, nick, from, op);
            } else {
              if (channel_active(chan)) {
                if ((reversing) && (chan->channel.key[0]))
                  add_mode(chan, '+', 'k', chan->channel.key);
                else if ((chan->key_prot[0]) && !glob_master(user)
                         && !chan_master(user))
                  add_mode(chan, '+', 'k', chan->key_prot);
              }
              my_setkey(chan, NULL);
            }
            break;
          case 'o':
            chan->channel.fighting++;
            op = newsplit(&msg);
            fixcolon(op);
            if (ms2[0] == '+')
              got_op(chan, nick, from, op, u, &user);
            else
              got_deop(chan, nick, from, op, u);
            break;
          case 'v':
            op = newsplit(&msg);
            fixcolon(op);
            m = ismember(chan, op);
            if (!m) {
              if (channel_pending(chan))
                break;
              putlog(LOG_MISC, chan->dname, CHAN_BADCHANMODE, chan->dname, op);
              dprintf(DP_MODE, "WHO %s\n", op);
            } else {
              int dv = 0;

              simple_sprintf(s, "%s!%s", m->nick, m->userhost);
              get_user_flagrec(m->user ? m->user : get_user_by_host(s), &victim, chan->dname);

              if (ms2[0] == '+') {
                if (m->flags & EVOICE) {
/* FIXME: This is a lame check, we need to expand on this more */
                  if (!chan_master(user) && !glob_master(user)) {
                    dv++;
                  } else {
                    m->flags &= ~EVOICE;
                  }
                }
                m->flags &= ~SENTVOICE;
                m->flags |= CHANVOICE;
                if (channel_active(chan) && dovoice(chan)) {
                  if (dv || chk_devoice(victim)) {
                    add_mode(chan, '-', 'v', op);
                  } else if (reversing) {
                    add_mode(chan, '-', 'v', op);
                  }
                }
              } else if (ms2[0] == '-') {
                m->flags &= ~SENTDEVOICE;
                m->flags &= ~CHANVOICE;
                if (channel_active(chan) && dovoice(chan)) {
                  /* revoice +v users */
                  if (chk_voice(victim, chan)) {
                    add_mode(chan, '+', 'v', op);
                  } else if (reversing) {
                    add_mode(chan, '+', 'v', op);
                    /* if they arent +v|v and VOICER is m+ then EVOICE them */
                  } else {
/* FIXME: same thing here */
                    if (!match_my_nick(nick) && channel_voice(chan) &&
                        (glob_master(user) || chan_master(user) || glob_bot(user))) {
                      /* if the user is not +q set them norEVOICE. */
                      if (!chan_quiet(victim) && !(m->flags & EVOICE)) {
                        putlog(LOG_DEBUG, "@", "Giving EVOICE flag to: %s (%s)", m->nick, chan->dname);
                        m->flags |= EVOICE;
                      }
                    }
                  }
                }
              }
            }
            break;
          case 'b':
            chan->channel.fighting++;
            op = newsplit(&msg);
            fixcolon(op);
            if (ms2[0] == '+')
              got_ban(chan, nick, from, op);
            else
              got_unban(chan, nick, from, op, u);
            break;
          case 'e':
            chan->channel.fighting++;
            op = newsplit(&msg);
            fixcolon(op);
            if (ms2[0] == '+')
              got_exempt(chan, nick, from, op);
            else
              got_unexempt(chan, nick, from, op, u);
            break;
          case 'I':
            chan->channel.fighting++;
            op = newsplit(&msg);
            fixcolon(op);
            if (ms2[0] == '+')
              got_invite(chan, nick, from, op);
            else
              got_uninvite(chan, nick, from, op, u);
            break;
        }
        if (todo) {
          if (ms2[0] == '+')
            chan->channel.mode |= todo;
          else
            chan->channel.mode &= ~todo;
          if (channel_active(chan)) {
            if ((((ms2[0] == '+') && (chan->mode_mns_prot & todo)) ||
                 ((ms2[0] == '-') && (chan->mode_pls_prot & todo))) &&
                !glob_master(user) && !chan_master(user))
              add_mode(chan, ms2[0] == '+' ? '-' : '+', *chg, "");
            else if (reversing &&
                     ((ms2[0] == '+') || (chan->mode_pls_prot & todo)) &&
                     ((ms2[0] == '-') || (chan->mode_mns_prot & todo)))
              add_mode(chan, ms2[0] == '+' ? '-' : '+', *chg, "");
          }
        }
        chg++;
      }
      if (chan->channel.do_opreq)
        request_op(chan);
      if (!me_op(chan) && !nick[0])
        chan->status |= CHAN_ASKEDMODES;
    }
  }
  return 0;
}

#endif /* LEAF */
