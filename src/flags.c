/*
 * flags.c -- handles:
 *   all the flag matching/conversion functions in one neat package :)
 *
 * $Id$
 */

#include "common.h"
#include "crypt.h"
#include "src/mod/share.mod/share.h"
#include "rfc1459.h"
#include "userrec.h"
#include "misc.h"
#include "dccutil.h"
#include "userent.h"
#include "users.h"

flag_t		FLAG[128];

void init_flags()
{
       unsigned char i;

       for (i = 0; i < 'A'; i++)
               FLAG[i] = 0;
       for (; i <= 'Z'; i++)
               FLAG[i] = (flag_t) 1 << (26 + (i - 'A'));
       for (; i < 'a'; i++)
               FLAG[i] = 0;
       for (; i <= 'z'; i++)
               FLAG[i] = (flag_t) 1 << (i - 'a');
       for (; i < 128; i++)
               FLAG[i] = 0;

//printf("a: %llX A: %llX\n",  FLAG['a'],  FLAG['Z']);
//printf("0: %llX 26: %llX\n",  BIT0, BIT31);
}

/* Some flags are mutually exclusive -- this roots them out
 */
int sanity_check(flag_t atr)
{
/* bots shouldnt have +pmcnaijlys */
  if ((atr & USER_BOT) &&
      (atr & (USER_PARTY | USER_MASTER | USER_OWNER | USER_ADMIN | USER_HUBA | USER_CHUBA)))
    atr &= ~(USER_PARTY | USER_MASTER | USER_OWNER | USER_ADMIN | USER_HUBA | USER_CHUBA);
/* only bots should be there: */
  if (!(atr & USER_BOT) &&
      (atr & (USER_DOLIMIT | USER_DOVOICE | USER_UPDATEHUB | USER_CHANHUB)))
    atr &= ~(USER_DOLIMIT | USER_DOVOICE | USER_UPDATEHUB | USER_CHANHUB);
  if ((atr & USER_OP) && (atr & USER_DEOP))
    atr &= ~(USER_OP | USER_DEOP);
  if ((atr & USER_VOICE) && (atr & USER_QUIET))
    atr &= ~(USER_VOICE | USER_QUIET);
 
  /* Can't be admin without also being owner and having hub access */
  if (atr & USER_ADMIN)
    atr |= USER_OWNER | USER_HUBA | USER_PARTY;
  /* Hub access gets chanhub access */
  if (atr & USER_HUBA)
    atr |= USER_CHUBA;
  if (atr & USER_OWNER) {
    atr |= USER_MASTER;
  }
  /* Master implies botmaster, op */
  if (atr & USER_MASTER)
    atr |= USER_OP | USER_CHUBA;
  /* Can't be botnet master without party-line access */
/*  if (atr & USER_BOTMAST)
    atr |= USER_PARTY;
*/
  return atr;
}

/* Sanity check on channel attributes
 */
int chan_sanity_check(int chatr, int atr)
{
  /* admin for chan does shit.. */
  if (chatr & USER_ADMIN)
    chatr &= ~(USER_ADMIN);
  if ((chatr & USER_OP) && (chatr & USER_DEOP))
    chatr &= ~(USER_OP | USER_DEOP);
  if ((chatr & USER_VOICE) && (chatr & USER_QUIET))
    chatr &= ~(USER_VOICE | USER_QUIET);
  /* Can't be channel owner without also being channel master */
  if (chatr & USER_OWNER)
    chatr |= USER_MASTER;
  /* Master implies op */
  if (chatr & USER_MASTER)
    chatr |= USER_OP ;
  return chatr;
}

/* Get icon symbol for a user (depending on access level)
 *
 * (*) owner on any channel
 * (+) master on any channel
 * (%) botnet master
 * (@) op on any channel
 * (-) other
 */
char geticon(int idx)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0};

  if (!dcc[idx].user)
    return '-';
  get_user_flagrec(dcc[idx].user, &fr, 0);
  if (glob_admin(fr))
    return '^';
  if (chan_owner(fr))
    return '*';
  if (chan_master(fr))
    return '+';
  if (chan_op(fr))
    return '@';
  return '-';
}

void break_down_flags(const char *string, struct flag_record *plus, struct flag_record *minus)
{
  struct flag_record	*which = plus;
  int			 mode = 0;	/* 0 = glob, 1 = chan, 2 = bot */
  int			 flags = plus->match;

  if (!(flags & FR_GLOBAL)) {
    if (flags & FR_BOT)
      mode = 2;
    else if (flags & FR_CHAN)
      mode = 1;
    else
      return;			/* We dont actually want any..huh? */
  }
  egg_bzero(plus, sizeof(struct flag_record));

  if (minus)
    egg_bzero(minus, sizeof(struct flag_record));

  plus->match = FR_OR;		/* Default binding type OR */
  while (*string) {
    switch (*string) {
    case '+':
      which = plus;
      break;
    case '-':
      which = minus ? minus : plus;
      break;
    case '|':
    case '&':
      if (!mode) {
	if (*string == '|')
	  plus->match = FR_OR;
	else
	  plus->match = FR_AND;
      }
      which = plus;
      mode++;
      if ((mode == 2) && !(flags & (FR_CHAN | FR_BOT)))
	string = "";
      else if (mode == 3)
	mode = 1;
      break;
    default:
      if ((*string >= 'a') && (*string <= 'z')) {
	switch (mode) {
	case 0:
	  /* which->global |= (flag_t) 1 << (*string - 'a'); */
          which->global |= FLAG[(unsigned char) *string];
	  break;
	case 1:
	  /* which->chan |= (flag_t) 1 << (*string - 'a'); */
          which->chan |= FLAG[(unsigned char) *string];
	  break;
	case 2:
	  /* which->bot |= (flag_t) 1 << (*string - 'a'); */
          which->bot |= FLAG[(unsigned char) *string];
	}
      } 
    }
    string++;
  }
  for (which = plus; which; which = (which == plus ? minus : 0)) {
    which->global &=USER_VALID;

    which->chan &= CHAN_VALID;
    which->bot &= BOT_VALID;
  }
  plus->match |= flags;
  if (minus) {
    minus->match |= flags;
    if (!(plus->match & (FR_AND | FR_OR)))
      plus->match |= FR_OR;
  }
}

static int flag2str(char *string, flag_t bot)
{
  char x = 'a', *old = string;

  while (bot && (x <= 'z')) {
    if (bot & 1)
      *string++ = x;
    x++;
    bot = bot >> 1;
  }
/* udef
  x = 'A';
  while (udef && (x <= 'Z')) {
    if (udef & 1)
      *string++ = x;
    udef = udef >> 1;
    x++;
  }
*/
  if (string == old)
    *string++ = '-';
  return string - old;
}

static int bot2str(char *string, int bot)
{
  char x = 'a', *old = string;

  while (x < 'v') {
    if (bot & 1)
      *string++ = x;
    x++;
    bot >>= 1;
  }
  x = '0';
  while (x <= '9') {
    if (bot & 1)
      *string++ = x;
    x++;
    bot >>= 1;
  }
  return string - old;
}

int build_flags(char *string, struct flag_record *plus, struct flag_record *minus)
{
  char *old = string;

  if (plus->match & FR_GLOBAL) {
    if (minus && plus->global)
      *string++ = '+';
    string += flag2str(string, plus->global);

    if (minus && minus->global) {
      *string++ = '-';
      string += flag2str(string, minus->global);
    }
  } else if (plus->match & FR_BOT) {
    if (minus && plus->bot)
      *string++ = '+';
    string += bot2str(string, plus->bot);
    if (minus && minus->bot) {
      *string++ = '-';
      string += bot2str(string, minus->bot);
    }
  }
  if (plus->match & FR_CHAN) {
    if (plus->match & (FR_GLOBAL | FR_BOT))
      *string++ = (plus->match & FR_AND) ? '&' : '|';
    if (minus && plus->chan)
      *string++ = '+';
    string += flag2str(string, plus->chan);
    if (minus && minus->chan) {
      *string++ = '-';
      string += flag2str(string, minus->global);
    }
  }
  if ((plus->match & (FR_BOT | FR_CHAN)) == (FR_BOT | FR_CHAN)) {
    *string++ = (plus->match & FR_AND) ? '&' : '|';
    if (minus && plus->bot)
      *string++ = '+';
    string += bot2str(string, plus->bot);
    if (minus && minus->bot) {
      *string++ = '-';
      string += bot2str(string, minus->bot);
    }
  }
  if (string == old) {
    *string++ = '-';
    *string = 0;
    return 0;
  }
  *string = 0;
  return string - old;
}

/* Returns 1 if flags match, 0 if they don't. */
int flagrec_ok(struct flag_record *req, struct flag_record *have)
{
  if (req->match & FR_AND) {
    return flagrec_eq(req, have);
  } else if (req->match & FR_OR) {
    int hav = have->global;

    /* Exception 1 - global +d/+k cant use -|-, unless they are +p */
    if (!req->chan && !req->global)
      return 1;
    /* The +n/+m checks arent needed anymore since +n/+m
     * automatically add lower flags
     */
/*    if (!1 && ((hav & USER_OP) || (have->chan & USER_OWNER)))
      hav |= USER_PARTY;*/
    if (hav & req->global)
      return 1;
    if (have->chan & req->chan)
      return 1;
    return 0;
  }
  return 0;			/* fr0k3 binding, dont pass it */
}

/* Returns 1 if flags match, 0 if they don't. */
int flagrec_eq(struct flag_record *req, struct flag_record *have)
{
  if (req->match & FR_AND) {
    if (req->match & FR_GLOBAL) {
      if ((req->global &have->global) !=req->global)
	return 0;
    }
    if (req->match & FR_BOT)
      if ((req->bot & have->bot) != req->bot)
	return 0;
    if (req->match & FR_CHAN) {
      if ((req->chan & have->chan) != req->chan)
	return 0;
    }
    return 1;
  } else if (req->match & FR_OR) {
    if (!req->chan && !req->global && !req->bot)
      return 1;
    if (req->match & FR_GLOBAL) {
      if (have->global &req->global)
	return 1;
    }
    if (req->match & FR_BOT)
      if (have->bot & req->bot)
	return 1;
    if (req->match & FR_CHAN) {
      if (have->chan & req->chan)
	return 1;
    }
    return 0;
  }
  return 0;			/* fr0k3 binding, dont pass it */
}

void set_user_flagrec(struct userrec *u, struct flag_record *fr, const char *chname)
{
  struct chanuserrec *cr = NULL;
  int oldflags = fr->match;
  char buffer[100] = "";
  struct chanset_t *ch;

  if (!u)
    return;
  if (oldflags & FR_GLOBAL) {
    u->flags = fr->global;

    if (!noshare) {
      fr->match = FR_GLOBAL;
      build_flags(buffer, fr, NULL);
      shareout(NULL, "a %s %s\n", u->handle, buffer);
    }
  }
  /* Don't share bot attrs */
  if ((oldflags & FR_CHAN) && chname) {
    for (cr = u->chanrec; cr; cr = cr->next)
      if (!rfc_casecmp(chname, cr->channel))
	break;
    ch = findchan_by_dname(chname);
    if (!cr && ch) {
      cr = calloc(1, sizeof(struct chanuserrec));

      cr->next = u->chanrec;
      u->chanrec = cr;
      strncpyz(cr->channel, chname, sizeof cr->channel);
    }
    if (cr && ch) {
      cr->flags = fr->chan;
      if (!noshare) {
	fr->match = FR_CHAN;
	build_flags(buffer, fr, NULL);
	shareout(ch, "a %s %s %s\n", u->handle, buffer, chname);
      }
    }
  }
  fr->match = oldflags;
}

/* Always pass the dname (display name) to this function for chname <cybah>
 */
void get_user_flagrec(struct userrec *u, struct flag_record *fr, const char *chname)
{
  struct chanuserrec *cr = NULL;

  if (!u) {
    fr->global = fr->chan = fr->bot = 0;
    return;
  }
  if (fr->match & FR_GLOBAL) {
    fr->global = u->flags;
  } else {
    fr->global = 0;
  }
  fr->bot = 0;
  if (fr->match & FR_CHAN) {
    if (fr->match & FR_ANYWH) {
      fr->chan = u->flags;
      for (cr = u->chanrec; cr; cr = cr->next)
	if (findchan_by_dname(cr->channel)) {
	  fr->chan |= cr->flags;
	}
    } else {
      if (chname) {
	for (cr = u->chanrec; cr; cr = cr->next) {
	  if (!rfc_casecmp(chname, cr->channel))
	    break;
        }
      }

      if (cr) {
	fr->chan = cr->flags;
      } else {
	fr->chan = 0;
      }
    }
  }
}

/* private returns 0 if user has access, and 1 if they dont because of +private
 * This function does not check if the user has "op" access, it only checks if the user is
 * restricted by +private for the channel
 */
int private(struct flag_record fr, struct chanset_t *chan, int type)
{
  if (!channel_private(chan) || glob_bot(fr) || glob_owner(fr))
    return 0; /* user is implicitly not restricted by +private, they may however be lacking other flags */

  if (type == PRIV_OP) {
    /* |o implies all flags above. n| has access to all +private. Bots are exempt. */
    if (chan_op(fr))
      return 0;
  } else if (type == PRIV_VOICE) {
    if (chan_voice(fr))
      return 0;
  }
  return 1; /* user is restricted by +private */
}

int chk_op(struct flag_record fr, struct chanset_t *chan)
{
  if (!chan || (!private(fr, chan, PRIV_OP) && !chk_deop(fr, chan))) {
    if (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))
      return 1;
  }
  return 0;
}

int chk_deop(struct flag_record fr, struct chanset_t *chan)
{
  if (chan_deop(fr) || (glob_deop(fr) && !chan_op(fr)))
    return 1;
  else
    return 0;
}

int chk_voice(struct flag_record fr, struct chanset_t *chan)
{
  if (!chan || (!private(fr, chan, PRIV_VOICE) && !chk_devoice(fr, chan))) {
    if (chan_voice(fr) || (glob_voice(fr) && !chan_quiet(fr)))
      return 1;
  }
  return 0;
}

int chk_devoice(struct flag_record fr, struct chanset_t *chan)
{
  if (chan_quiet(fr) || (glob_quiet(fr) && !chan_voice(fr)))
    return 1;
  else
    return 0;
}

int chk_noflood(struct flag_record fr, struct chanset_t *chan)
{
  return (chan_noflood(fr) || glob_noflood(fr));
}

int isupdatehub()
{
#ifdef HUB
  if (conf.bot->u && (conf.bot->u->flags & USER_UPDATEHUB))
    return 1;
  else
#endif /* HUB */
    return 0;
}

int ischanhub()
{
  if (conf.bot->u && (conf.bot->u->flags & USER_CHANHUB))
    return 1;
  else
    return 0;
}

int dovoice(struct chanset_t *chan)
{
  struct flag_record fr = { FR_GLOBAL | FR_CHAN, 0, 0, 0};

  if (!chan) return 0;

  get_user_flagrec(conf.bot->u, &fr, chan->dname);
  if (glob_dovoice(fr) || chan_dovoice(fr))
    return 1;
  return 0;
}

int dolimit(struct chanset_t *chan)
{
  struct flag_record fr = { FR_GLOBAL | FR_CHAN, 0, 0, 0};

  if (!chan) return 0;

  get_user_flagrec(conf.bot->u, &fr, chan->dname);
  if (glob_dolimit(fr) || chan_dolimit(fr))
    return 1;
  return 0;
}

int whois_access(struct userrec *user, struct userrec *whois_user)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0}, whois = {FR_GLOBAL | FR_CHAN, 0, 0, 0};

  get_user_flagrec(user, &fr, NULL);
  get_user_flagrec(whois_user, &whois, NULL);

  if ((glob_master(whois) && !glob_master(fr)) ||
     (glob_owner(whois) && !glob_owner(fr)) ||
     (glob_admin(whois) && !glob_admin(fr)) ||
     (glob_bot(whois) && !glob_master(fr)))
    return 0;
  return 1;
}

