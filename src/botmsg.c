/*
 * botmsg.c -- handles:
 *   formatting of messages to be sent on the botnet
 *   sending differnet messages to different versioned bots
 *
 * by Darrin Smith (beldin@light.iinet.net.au)
 *
 * $Id$
 */

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

#include "common.h"
#include "misc.h"
#include "base64.h"
#include "dcc.h"
#include "userrec.h"
#include "main.h"
#include "net.h"
#include "users.h"
#include "cfg.h"
#include "botmsg.h"
#include "dccutil.h"
#include "cmds.h"
#include "chanprog.h"
#include "botnet.h"
#include "tandem.h"
#include "core_binds.h"
#include "src/mod/notes.mod/notes.h"
#include <stdarg.h>

static char	OBUF[SGRAB - 110] = "";

/* Ditto for tandem bots
 */
static void send_tand_but(int x, char *buf, size_t len)
{
  for (int i = 0; i < dcc_total; i++) {
    if (dcc[i].type && (dcc[i].type == &DCC_BOT) && i != x) {
      tputs(dcc[i].sock, buf, len);
    }
  }
}

void botnet_send_cmdpass(int idx, char *cmd, char *pass)
{
  if (tands > 0) {
    char *buf = (char *) my_calloc(1, strlen(cmd) + strlen(pass) + 5 + 1);

    simple_sprintf(buf, "cp %s %s\n", cmd, pass);
    send_tand_but(idx, buf, strlen(buf));
    free(buf);
  }
}

int botnet_send_cmd(char * fbot, char * bot, char *fhnd, int fromidx, char * cmd) {
  int i = nextbot(bot);

  if (i >= 0) {
    simple_sprintf(OBUF, "rc %s %s %s %i %s\n", bot, fbot, fhnd, fromidx, cmd);
    tputs(dcc[i].sock, OBUF, strlen(OBUF));
    return 1;
  } else if (!strcmp(bot, conf.bot->nick)) {
    char tmp[24] = "";

    simple_sprintf(tmp, "%i", fromidx);
    gotremotecmd(conf.bot->nick, conf.bot->nick, fhnd, tmp, cmd);
  }
  return 0;
}

void botnet_send_cmd_broad(int idx, char * fbot, char *fhnd, int fromidx, char * cmd) {
  if (tands > 0) {
    simple_snprintf(OBUF, sizeof OBUF, "rc * %s %s %i %s\n", fbot, fhnd, fromidx, cmd);
    send_tand_but(idx, OBUF, strlen(OBUF));
  }
  if (idx < 0) {
    char tmp[24] = "";

    simple_sprintf(tmp, "%i", fromidx);
    gotremotecmd("*", conf.bot->nick, fhnd, tmp, cmd);
  }
}

void botnet_send_cmdreply(char * fbot, char * bot, char * to, char * toidx, char * ln) {
  int i = nextbot(bot);

  if (i >= 0) {
    simple_snprintf(OBUF, sizeof OBUF, "rr %s %s %s %s %s\n", bot, fbot, to, toidx, ln);
    tputs(dcc[i].sock, OBUF, strlen(OBUF));
  } else if (!strcmp(bot, conf.bot->nick)) {
    gotremotereply(conf.bot->nick, to, toidx, ln);
  }
}


void botnet_send_bye()
{
  if (tands > 0)
    send_tand_but(-1, "bye\n", 5);
}

void botnet_send_chan(int idx, char *botnick, char *user, int chan, char *data)
{
  if ((tands > 0) && (chan < GLOBAL_CHANS)) {
    size_t len;

    if (user) {
      len = simple_sprintf(OBUF, "c %s@%s %s %s\n", user, botnick, int_to_base64(chan), data);
    } else {
      len = simple_sprintf(OBUF, "c %s %s %s\n", botnick, int_to_base64(chan), data);
    }
    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_act(int idx, char *botnick, char *user, int chan, char *data)
{
  if ((tands > 0) && (chan < GLOBAL_CHANS)) {
    size_t len;

    if (user) {
      len = simple_sprintf(OBUF, "a %s@%s %s %s\n", user, botnick, int_to_base64(chan), data);
    } else {
      len = simple_sprintf(OBUF, "a %s %s %s\n", botnick, int_to_base64(chan), data);
    }
    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_chat(int idx, char *botnick, char *data)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "ct %s %s\n", botnick, data);

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_ping(int idx)
{
  tputs(dcc[idx].sock, "pi\n", 3);
  dcc[idx].pingtime = now;
}

void botnet_send_pong(int idx)
{
  tputs(dcc[idx].sock, "po\n", 3);
}

void botnet_send_priv (int idx, char *from, char *to, char *tobot, const char *format, ...)
{
  size_t len;
  char tbuf[1024] = "";
  va_list va;

  va_start(va, format);
  egg_vsnprintf(tbuf, sizeof(tbuf), format, va);
  va_end(va);

  if (tobot) {
    len = simple_sprintf(OBUF, "p %s %s@%s %s\n", from, to, tobot, tbuf);
  } else {
    len = simple_sprintf(OBUF, "p %s %s %s\n", from, to, tbuf);
  }
  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_who(int idx, char *from, char *to, int chan)
{
  size_t len = simple_sprintf(OBUF, "w %s %s %s\n", from, to, int_to_base64(chan));

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_unlink(int idx, char *who, char *via, char *bot, char *reason)
{
  size_t len = simple_sprintf(OBUF, "ul %s %s %s %s\n", who, via, bot, reason);

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_link(int idx, char *who, char *via, char *bot)
{
  size_t len = simple_sprintf(OBUF, "l %s %s %s\n", who, via, bot);

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_unlinked(int idx, char *bot, char *args)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "un %s %s\n", bot, args ? args : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_nlinked(int idx, char *bot, char *next, char flag, int vlocalhub, time_t vbuildts, char *vversion)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "n %s %s %cD0gc %d %d %s\n", bot, next, flag, 
                                       vlocalhub, (int) vbuildts, vversion ? vversion : "");
    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_traced(int idx, char *bot, char *buf)
{
  size_t len = simple_sprintf(OBUF, "td %s %s\n", bot, buf);

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_trace(int idx, char *to, char *from, char *buf)
{
  size_t len = simple_sprintf(OBUF, "t %s %s %s:%s\n", to, from, buf, conf.bot->nick);

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_update(int idx, tand_t * ptr)
{
  if (tands > 0) {
    /* the D0gc is a lingering hack which probably will never be able to come out. */
    size_t len = simple_sprintf(OBUF, "u %s %cD0gc %d %d %s\n", ptr->bot, ptr->share, ptr->localhub, 
                                                          (int) ptr->buildts, ptr->version ? ptr->version : "");
    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_reject(int idx, char *fromp, char *frombot, char *top, char *tobot, char *reason)
{
  size_t len;
  char to[NOTENAMELEN + 1] = "", from[NOTENAMELEN + 1] = "";

  if (tobot) {
    simple_sprintf(to, "%s@%s", top, tobot);
    top = to;
  }
  if (frombot) {
    simple_sprintf(from, "%s@%s", fromp, frombot);
    fromp = from;
  }
  if (!reason)
    reason = "";
  len = simple_sprintf(OBUF, "r %s %s %s\n", fromp, top, reason);
  tputs(dcc[idx].sock, OBUF, len);
}

void putallbots(char *par)
{ 
  botnet_send_zapf_broad(-1, conf.bot->nick, NULL, par);

  return;
}

void putbot(char *bot, char *par)
{
  if (!bot || !par || !bot[0] || !par[0])
    return;

  int i = nextbot(bot);

  if (i < 0)
    return;

  botnet_send_zapf(i, conf.bot->nick, bot, par);
}

void botnet_send_zapf(int idx, char *a, char *b, char *c)
{
  size_t len = simple_sprintf(OBUF, "z %s %s %s\n", a, b, c);

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_zapf_broad(int idx, char *a, char *b, char *c)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "zb %s %s%s%s\n", a, b ? b : "", b ? " " : "", c);
    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_cfg(int idx, struct cfg_entry * entry) {
  size_t len = simple_sprintf(OBUF, "cg %s %s\n", entry->name, entry->gdata ? entry->gdata : "");

  tputs(dcc[idx].sock, OBUF, len);
}

void botnet_send_cfg_broad(int idx, struct cfg_entry * entry) {
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "cgb %s %s\n", entry->name, entry->gdata ? entry->gdata : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_idle(int idx, char *bot, int sock, int idle, char *away)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "i %s %s %ss%s\n", bot, int_to_base64(sock), int_to_base64(idle), away ? away : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_away(int idx, char *bot, int sock, char *msg, int linking)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "aw %s%s %s %s\n", ((idx >= 0) && linking) ? "!" : "", bot, int_to_base64(sock), 
                                msg ? msg : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_join_idx(int useridx)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "j %s %s %s %c%s %s\n",
		       conf.bot->nick, dcc[useridx].nick,
		       dcc[useridx].type && dcc[useridx].type == &DCC_RELAYING ? 
                         int_to_base64(dcc[useridx].u.relay->chat->channel) : 
                         int_to_base64(dcc[useridx].u.chat->channel), geticon(useridx),
		         int_to_base64(dcc[useridx].sock), dcc[useridx].host);

    send_tand_but(-1, OBUF, len);
  }
}

void botnet_send_join_party(int idx, int linking, int useridx)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "j %s%s %s %s %c%s %s\n", linking ? "!" : "",
		       party[useridx].bot, party[useridx].nick,
		       int_to_base64(party[useridx].chan), party[useridx].flag,
		       int_to_base64(party[useridx].sock),
		       party[useridx].from ? party[useridx].from : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_part_idx(int useridx, char *reason)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "pt %s %s %s %s\n", conf.bot->nick,
			 dcc[useridx].nick, int_to_base64(dcc[useridx].sock),
			 reason ? reason : "");

    send_tand_but(-1, OBUF, len);
  }
}

void botnet_send_part_party(int idx, int partyidx, char *reason, int silent)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "pt %s%s %s %s %s\n",
		       silent ? "!" : "", party[partyidx].bot,
		       party[partyidx].nick, int_to_base64(party[partyidx].sock),
		       reason ? reason : "");

    send_tand_but(idx, OBUF, len);
  }
}

void botnet_send_nkch(int useridx, char *oldnick)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "nc %s %s %s\n", conf.bot->nick, int_to_base64(dcc[useridx].sock), dcc[useridx].nick);

    send_tand_but(-1, OBUF, len);
  }
}

void botnet_send_nkch_part(int butidx, int useridx, char *oldnick)
{
  if (tands > 0) {
    size_t len = simple_sprintf(OBUF, "nc %s %s %s\n", party[useridx].bot, int_to_base64(party[useridx].sock), party[useridx].nick);

    send_tand_but(butidx, OBUF, len);
  }
}

/* This part of add_note is more relevant to the botnet than
 * to the notes file
 */
int add_note(char *to, char *from, char *msg, int idx, int echo)
{
  int status, i, iaway, sock;
  char *p = NULL, botf[81] = "", ss[81] = "", ssf[81] = "";
  struct userrec *u;

  if (strlen(msg) > 450)
    msg[450] = 0;		/* Notes have a limit */
  /* note length + PRIVMSG header + nickname + date  must be <512  */
  p = strchr(to, '@');
  if (p != NULL) {		/* Cross-bot note */
    char x[21] = "";

    *p = 0;
    strncpy(x, to, 20);
    x[20] = 0;
    *p = '@';
    p++;
    if (!egg_strcasecmp(p, conf.bot->nick))	/* To me?? */
      return add_note(x, from, msg, idx, echo); /* Start over, dimwit. */
    if (egg_strcasecmp(from, conf.bot->nick)) {
      if (strlen(from) > 40)
	from[40] = 0;
      if (strchr(from, '@')) {
	strcpy(botf, from);
      } else
	simple_sprintf(botf, "%s@%s", from, conf.bot->nick);
    } else
      strcpy(botf, conf.bot->nick);
    i = nextbot(p);
    if (i < 0) {
      if (idx >= 0)
	dprintf(idx, BOT_NOTHERE);
      return NOTE_ERROR;
    }
    if ((idx >= 0) && (echo))
      dprintf(idx, "-> %s@%s: %s\n", x, p, msg);
    if (idx >= 0) {
      simple_sprintf(ssf, "%d:%s", dcc[idx].sock, botf);
      botnet_send_priv(i, ssf, x, p, "%s", msg);
    } else
      botnet_send_priv(i, botf, x, p, "%s", msg);
    return NOTE_OK;		/* Forwarded to the right bot */
  }
  /* Might be form "sock:nick" */
  splitc(ssf, from, ':');
  rmspace(ssf);
  splitc(ss, to, ':');
  rmspace(ss);
  if (!ss[0])
    sock = (-1);
  else
    sock = atoi(ss);
  /* Don't process if there's a note binding for it */
  if (idx != (-2)) {		/* Notes from bots don't trigger it */
    if (check_bind_note(from, to, msg)) {
      if ((idx >= 0) && (echo))
	dprintf(idx, "-> %s: %s\n", to, msg);
      return NOTE_TCL;
    }
  }
  if (!(u = get_user_by_handle(userlist, to))) {
    if (idx >= 0)
      dprintf(idx, USERF_UNKNOWN);
    return NOTE_ERROR;
  }
  if (is_bot(u)) {
    if (idx >= 0)
      dprintf(idx, BOT_NONOTES);
    return NOTE_ERROR;
  }

  status = NOTE_STORED;
  iaway = 0;
  /* Online right now? */
  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type && (dcc[i].type->flags & DCT_GETNOTES) &&
	((sock == (-1)) || (sock == dcc[i].sock)) &&
	(!egg_strcasecmp(dcc[i].nick, to))) {
      int aok = 1;

      if (dcc[i].type == &DCC_CHAT)
	if ((dcc[i].u.chat->away != NULL) &&
	    (idx != (-2))) {
	  /* Only check away if it's not from a bot */
	  aok = 0;
	  if (idx >= 0)
	    dprintf(idx, "%s %s: %s\n", dcc[i].nick, BOT_USERAWAY,
		    dcc[i].u.chat->away);
	  if (!iaway)
	    iaway = i;
	  status = NOTE_AWAY;
	}
      if (aok) {
	char *p2 = NULL, *fr = from;
	int l = 0;
	char work[1024] = "";

	while ((*msg == '<') || (*msg == '>')) {
	  p2 = newsplit(&msg);
	  if (*p2 == '<')
	    l += simple_sprintf(work + l, "via %s, ", p2 + 1);
	  else if (*from == '@')
	    fr = p2 + 1;
	}
	if (idx == -2 || (!egg_strcasecmp(from, conf.bot->nick)))
	  dprintf(i, "*** [%s] %s%s\n", fr, l ? work : "", msg);
	else
	  dprintf(i, "%cNote [%s]: %s%s\n", 7, fr, l ? work : "", msg);
	if ((idx >= 0) && (echo))
	  dprintf(idx, "-> %s: %s\n", to, msg);
	return NOTE_OK;
      }
    }
  }
  if (idx == (-2))
    return NOTE_OK;		/* Error msg from a tandembot: don't store */
  status = storenote(from, to, msg, idx, NULL, 0);
  if (status < 0) status = NOTE_ERROR;
  else if (status == NOTE_AWAY) {
      /* User is away in all sessions -- just notify the user that a
       * message arrived and was stored. (only oldest session is notified.)
       */
      dprintf(iaway, "*** %s.\n", BOT_NOTEARRIVED);
  }
  return(status);
}
