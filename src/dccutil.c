/*
 * dccutil.c -- handles:
 *   lots of little functions to send formatted text to
 *   varying types of connections
 *   '.who', '.whom', and '.dccstat' code
 *   memory management for dcc structures
 *   timeout checking for dcc connections
 *
 * $Id$
 */

#include <sys/stat.h>
#include "common.h"
#include "color.h"
#include "dcc.h"
#include "botnet.h"
#include "net.h"
#include "main.h"
#include "dccutil.h"
#include "misc.h"
#include "botcmd.h"
#include <errno.h>
#include "chan.h"
#include "tandem.h"
#include "core_binds.h"
#include "src/mod/server.mod/server.h"
#include <stdarg.h>

static struct portmap 	*root = NULL;

int	connect_timeout = 15;		/* How long to wait before a telnet
					   connection times out */
int         max_dcc = 200;              /* needs at least 4 or 5 just to
                                           get started. 20 should be enough   */
static int         dcc_flood_thr = 3;

void init_dcc_max()
{
  int osock = MAXSOCKS;
  if (max_dcc < 1)
    max_dcc = 1;
  if (dcc)
    dcc = realloc(dcc, sizeof(struct dcc_t) * max_dcc);
  else 
    dcc = calloc(1, sizeof(struct dcc_t) * max_dcc);

  MAXSOCKS = max_dcc + 10;
  if (socklist)
    socklist = (sock_list *) realloc((void *) socklist, sizeof(sock_list) * MAXSOCKS);
  else
    socklist = (sock_list *) calloc(1, sizeof(sock_list) * MAXSOCKS);

  for (; osock < MAXSOCKS; osock++)
    socklist[osock].flags = SOCK_UNUSED;

}

/* Replace \n with \r\n */
char *add_cr(char *buf)
{
  static char WBUF[1024] = "";
  char *p = NULL, *q = NULL;

  for (p = buf, q = WBUF; *p; p++, q++) {
    if (*p == '\n')
      *q++ = '\r';
    *q = *p;
  }
  *q = *p;
  return WBUF;
}

void dprintf (int idx, ...)
{
  static char buf[1024] = "";
  char *format = NULL;
  int len;
  va_list va;

  va_start(va, idx);
  format = va_arg(va, char *);
  egg_vsnprintf(buf, 1023, format, va);
  va_end(va);
  /* We can not use the return value vsnprintf() to determine where
   * to null terminate. The C99 standard specifies that vsnprintf()
   * shall return the number of bytes that would be written if the
   * buffer had been large enough, rather then -1.
   */
  /* We actually can, since if it's < 0 or >= sizeof(buf), we know it wrote
   * sizeof(buf) bytes. But we're not doing that anyway.
  */
  buf[sizeof(buf) - 1] = 0;
  len = strlen(buf);

/* this is for color on dcc :P */

  if (idx < 0) {
    tputs(-idx, buf, len);
  } else if (idx > 0x7FF0) {
    switch (idx) {
    case DP_LOG:
      putlog(LOG_MISC, "*", "%s", buf);
      break;
    case DP_STDOUT:
      tputs(STDOUT, buf, len);
      break;
    case DP_STDERR:
      tputs(STDERR, buf, len);
      break;
    case DP_SERVER:
#ifdef HUB
     return;
#endif /* HUB */
    case DP_HELP:
#ifdef HUB
     return;
#endif /* HUB */
    case DP_MODE:
#ifdef HUB
     return;
#endif /* HUB */
    case DP_MODE_NEXT:
#ifdef HUB
     return;
#endif /* HUB */
    case DP_SERVER_NEXT:
#ifdef HUB
     return;
#endif /* HUB */
    case DP_HELP_NEXT:
#ifdef HUB
     return;
#endif /* HUB */
      queue_server(idx, buf, len);
      break;
    }
    return;
  } else { /* normal chat text */
    if ((dcc[idx].status & STAT_COLOR) && (dcc[idx].type == &DCC_CHAT)) {
      int i;
      char buf3[1024] = "", buf2[1024] = "", c = 0;

      for (i = 0 ; i < len ; i++) {
/* FIXME: Trying to fix bug where you do .color on ANSI, then .bc <bot> help help OVER TELNET
        if (buf[i] == '\033') {
          unsigned char *e;
          for (e = buf + 2; *e != 'm' && *e; e++)
            i++;
          if (i >= len) break;
        }
*/
        c = buf[i];
        buf2[0] = 0;

        if (c == ':') {
          sprintf(buf2, "%s%c%s", LIGHTGREY(idx), c, COLOR_END(idx));
        } else if (c == '@') {
          sprintf(buf2, "%s%c%s", BOLD(idx), c, BOLD_END(idx));
/*        } else if (c == ']' || c == '>' || c == ')' || c == '[' || c == '<' || c == '(') { */
        } else if (c == '>' || c == ')' || c == '<' || c == '(') {
          sprintf(buf2, "%s%c%s", GREEN(idx), c, COLOR_END(idx));
        } else {
          sprintf(buf2, "%c", c);
        }
/*        sprintf(buf3, "%s%s", buf3 ? buf3 : "", buf2 ? buf2 : ""); */
        sprintf(buf3, "%s%s", (buf3 && buf3[0]) ? buf3 : "", (buf2 && buf2[0]) ? buf2 : "");
      }
      buf3[strlen(buf3)] = 0;
      strcpy(buf, buf3);
/*      strncpyz(buf, buf2, sizeof buf); */
    }
    buf[sizeof(buf) - 1] = 0;
    len = strlen(buf);

    if (len > 1000) {		/* Truncate to fit */
      buf[1000] = 0;
      strcat(buf, "\n");
      len = 1001;
    }
    if (dcc[idx].simul > 0) {
      bounce_simul(idx, buf);
    } else {
      if (dcc[idx].type && ((long) (dcc[idx].type->output) == 1)) {
        char *p = add_cr(buf);

        tputs(dcc[idx].sock, p, strlen(p));
      } else if (dcc[idx].type && dcc[idx].type->output) {
        dcc[idx].type->output(idx, buf, dcc[idx].u.other);
      } else {
        tputs(dcc[idx].sock, buf, len);
      }
    }
  }
}

void chatout (char *format, ...)
{
  int i, len;
  char s[601] = "";
  va_list va;

  va_start(va, format);
  egg_vsnprintf(s, 511, format, va);
  va_end(va);
  len = strlen(s);
  if (len > 511)
    len = 511;
  s[len + 1] = 0;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_CHAT) && !(dcc[i].simul))
      if (dcc[i].u.chat->channel >= 0)
        dprintf(i, "%s", s);

}

/* Print to all on this channel but one.
 */
void chanout_but (int x, ...)
{
  int i, chan, len;
  char *format = NULL;
  char s[601] = "";
  va_list va;

  va_start(va, x);
  chan = va_arg(va, int);
  format = va_arg(va, char *);
  egg_vsnprintf(s, 511, format, va);
  va_end(va);
  len = strlen(s);
  if (len > 511)
    len = 511;
  s[len + 1] = 0;

  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_CHAT) && (i != x) && !(dcc[i].simul))
      if (dcc[i].u.chat->channel == chan)
        dprintf(i, "%s", s);
}

void dcc_chatter(int idx)
{
  int i, j;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};

  get_user_flagrec(dcc[idx].user, &fr, NULL);
  dprintf(idx, "Connected to %s, running %s\n", conf.bot->nick, version);
  show_banner(idx);
  show_motd(idx);
  if (glob_master(fr)) {
    if ((tands+1) > 1)
      dprintf(idx, STR("There are \002-%d- bots\002 currently linked.\n"), tands + 1);
    else
      dprintf(idx, STR("There is \002-%d- bot\002 currently linked.\n"), tands + 1);
  }
  show_channels(idx, NULL);

  if (glob_party(fr)) {
     i = dcc[idx].u.chat->channel;
  } else {
     dprintf(idx, "You don't have partyline chat access; commands only.\n\n");
     i = -1;
  }

  j = dcc[idx].sock;
  strcpy(dcc[idx].u.chat->con_chan, "***");
  check_bind_chon(dcc[idx].nick, idx);
  dcc[idx].u.chat->channel = 234567;
  /* Still there? */

  if ((idx >= dcc_total) || (dcc[idx].sock != j))
    return;			/* Nope */

  if (dcc[idx].type == &DCC_CHAT) {
    if (!strcmp(dcc[idx].u.chat->con_chan, "***"))
      strcpy(dcc[idx].u.chat->con_chan, "*");
    if (dcc[idx].u.chat->channel == 234567) {
      /* If the chat channel has already been altered it's *highly*
       * probably join/part messages have been broadcast everywhere,
       * so dont bother sending them
       */
      if (i == -2)
	i = 0;

      dcc[idx].u.chat->channel = i;

      if (dcc[idx].u.chat->channel >= 0) {
	if (dcc[idx].u.chat->channel < GLOBAL_CHANS) {
	  botnet_send_join_idx(idx, -1);
	}
      }
      check_bind_chjn(conf.bot->nick, dcc[idx].nick, dcc[idx].u.chat->channel,
		     geticon(idx), dcc[idx].sock, dcc[idx].host);
    }
    /* But *do* bother with sending it locally */

    if (!dcc[idx].u.chat->channel) {
      chanout_but(-1, 0, "*** %s joined the party line.\n", dcc[idx].nick);
    } else if (dcc[idx].u.chat->channel > 0) {
      chanout_but(-1, dcc[idx].u.chat->channel,
		  "*** %s joined the channel.\n", dcc[idx].nick);
    }
  }
}

/* Mark an entry as lost and deconstruct it's contents. It will be securely
 * removed from the dcc list in the main loop.
 */
void lostdcc(int n)
{

  /* Make sure it's a valid dcc index. */
  if (n < 0 || n >= max_dcc) return;

  if (dcc[n].type && dcc[n].type->kill)
    dcc[n].type->kill(n, dcc[n].u.other);
  else if (dcc[n].u.other)
    free(dcc[n].u.other);

  egg_bzero(&dcc[n], sizeof(struct dcc_t));

  dcc[n].sock = -1;
  dcc[n].type = &DCC_LOST;
}

/* Remove entry from dcc list. Think twice before using this function,
 * because it invalidates any variables that point to a specific dcc
 * entry!
 *
 * Note: The entry will be deconstructed if it was not deconstructed
 *       already. This case should normally not occur.
 */
void removedcc(int n)
{
  if (dcc[n].type && dcc[n].type->kill)
    dcc[n].type->kill(n, dcc[n].u.other);
  else if (dcc[n].u.other)
    free(dcc[n].u.other);

  dcc_total--;

  if (n < dcc_total)
    egg_memcpy(&dcc[n], &dcc[dcc_total], sizeof(struct dcc_t));
  else
    egg_bzero(&dcc[n], sizeof(struct dcc_t)); /* drummer */
}

/* Clean up sockets that were just left for dead.
 */
void dcc_remove_lost(void)
{
  int i;

  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type == &DCC_LOST) {
      dcc[i].type = NULL;
      dcc[i].sock = -1;
      removedcc(i);
      i--;
    }
  }
#ifdef LEAF
  servidx = findanyidx(serv);		/* servidx may have moved :\ */
#endif /* LEAF */
}

/* Show list of current dcc's to a dcc-chatter
 * positive value: idx given -- negative value: sock given
 */
void tell_dcc(int idx)
{
  int i, j, nicklen = 0;
  char other[160] = "", format[81] = "";

  /* calculate max nicklen */
  for (i = 0; i < dcc_total; i++) {
      if(strlen(dcc[i].nick) > nicklen)
          nicklen = strlen(dcc[i].nick);
  }
  if(nicklen < 9) nicklen = 9;
  
  egg_snprintf(format, sizeof format, "%%-4s %%-4s %%-8s %%-5s %%-%us %%-40s %%s\n", nicklen);
  dprintf(idx, format, "SOCK", "IDX", "ADDR",     "PORT",  "NICK", "HOST", "TYPE");
  dprintf(idx, format, "----", "---", "--------", "-----", "---------", 
                        "----------------------------------------", "----");

  egg_snprintf(format, sizeof format, "%%-4d %%-4d %%08X %%5d %%-%us %%-40s %%s\n", nicklen);

  for (i = 0; i < dcc_total; i++) {
    j = strlen(dcc[i].host);
    if (j > 40)
      j -= 40;
    else
      j = 0;
    if (dcc[i].type && dcc[i].type->display)
      dcc[i].type->display(i, other);
    else {
      sprintf(other, "?:%lX  !! ERROR !!", (long) dcc[i].type);
      break;
    }
    if (dcc[i].type == &DCC_LOST)
      dprintf(idx, "LOST:\n");
    dprintf(idx, format, dcc[i].sock, i, dcc[i].addr, dcc[i].port, dcc[i].nick, dcc[i].host + j, other);
  }
}

/* Mark someone on dcc chat as no longer away
 */
void not_away(int idx)
{
  if (dcc[idx].u.chat->away == NULL) {
    dprintf(idx, "You weren't away!\n");
    return;
  }
  if (dcc[idx].u.chat->channel >= 0) {
    chanout_but(-1, dcc[idx].u.chat->channel,
		"*** %s is no longer away.\n", dcc[idx].nick);
    if (dcc[idx].u.chat->channel < GLOBAL_CHANS) {
      botnet_send_away(-1, conf.bot->nick, dcc[idx].sock, NULL, idx);
    }
  }
  dprintf(idx, "You're not away any more.\n");
  free(dcc[idx].u.chat->away);
  dcc[idx].u.chat->away = NULL;
  check_bind_away(conf.bot->nick, idx, NULL);
}

void set_away(int idx, char *s)
{
  if (s == NULL) {
    not_away(idx);
    return;
  }
  if (!s[0]) {
    not_away(idx);
    return;
  }
  if (dcc[idx].u.chat->away != NULL)
    free(dcc[idx].u.chat->away);
  dcc[idx].u.chat->away = strdup(s);
  if (dcc[idx].u.chat->channel >= 0) {
    chanout_but(-1, dcc[idx].u.chat->channel, "*** %s is now away: %s\n", dcc[idx].nick, s);
    if (dcc[idx].u.chat->channel < GLOBAL_CHANS) {
      botnet_send_away(-1, conf.bot->nick, dcc[idx].sock, s, idx);
    }
  }
  dprintf(idx, "You are now away.\n");
  check_bind_away(conf.bot->nick, idx, s);
}


/* Make a password, 10-15 random letters and digits
 */
void makepass(char *s)
{
  make_rand_str(s, 10 + randint(6));
}

void flush_lines(int idx, struct chat_info *ci)
{
  int c = ci->line_count;
  struct msgq *p = ci->buffer, *o;

  while (p && c < (ci->max_line)) {
    ci->current_lines--;
    tputs(dcc[idx].sock, p->msg, p->len);
    free(p->msg);
    o = p->next;
    free(p);
    p = o;
    c++;
  }
  if (p != NULL) {
    if (dcc[idx].status & STAT_TELNET)
      tputs(dcc[idx].sock, "[More]: ", 8);
    else
      tputs(dcc[idx].sock, "[More]\n", 7);
  }
  ci->buffer = p;
  ci->line_count = 0;
}

int new_dcc(struct dcc_table *type, int xtra_size)
{
  int i = dcc_total;

  if (dcc_total == max_dcc)
    return -1;
  dcc_total++;
  egg_bzero((char *) &dcc[i], sizeof(struct dcc_t));

  dcc[i].type = type;
  if (xtra_size)
    dcc[i].u.other = calloc(1, xtra_size);

  return i;
}

/* Changes the given dcc entry to another type.
 */
void changeover_dcc(int i, struct dcc_table *type, int xtra_size)
{
  /* Free old structure. */
  if (dcc[i].type && dcc[i].type->kill)
    dcc[i].type->kill(i, dcc[i].u.other);
  else if (dcc[i].u.other) {
    free(dcc[i].u.other);
    dcc[i].u.other = NULL;
  }
  dcc[i].type = type;
  if (xtra_size)
    dcc[i].u.other = calloc(1, xtra_size);
}

int detect_dcc_flood(time_t * timer, struct chat_info *chat, int idx)
{
  time_t t;

  if (!dcc_flood_thr)
    return 0;
  t = now;
  if (*timer != t) {
    *timer = t;
    chat->msgs_per_sec = 0;
  } else {
    chat->msgs_per_sec++;
    if (chat->msgs_per_sec > dcc_flood_thr) {
      /* FLOOD */
      dprintf(idx, "*** FLOOD: %s.\n", IRC_GOODBYE);
      /* Evil assumption here that flags&DCT_CHAT implies chat type */
      if ((dcc[idx].type->flags & DCT_CHAT) && chat &&
	  (chat->channel >= 0)) {
	char x[1024];

	egg_snprintf(x, sizeof x, DCC_FLOODBOOT, dcc[idx].nick);
	chanout_but(idx, chat->channel, "*** %s", x);
	if (chat->channel < GLOBAL_CHANS)
	  botnet_send_part_idx(idx, x);
      }
      check_bind_chof(dcc[idx].nick, idx);
      if ((dcc[idx].sock != STDOUT) || backgrd) {
	killsock(dcc[idx].sock);
	lostdcc(idx);
      } else {
	dprintf(DP_STDOUT, "\n### SIMULATION RESET ###\n\n");
	dcc_chatter(idx);
      }
      return 1;			/* <- flood */
    }
  }
  return 0;
}

/* Handle someone being booted from dcc chat.
 */
void do_boot(int idx, char *by, char *reason)
{
  int files = (dcc[idx].type != &DCC_CHAT);

  dprintf(idx, DCC_BOOTED1);
  dprintf(idx, DCC_BOOTED2, files ? "file section" : "bot", by, reason[0] ? ": " : ".", reason);
  /* If it's a partyliner (chatterer :) */
  /* Horrible assumption that DCT_CHAT using structure uses same format
   * as DCC_CHAT */
  if ((dcc[idx].type->flags & DCT_CHAT) && (dcc[idx].u.chat->channel >= 0)) {
    char x[1024] = "";

    egg_snprintf(x, sizeof x, DCC_BOOTED3, by, dcc[idx].nick, reason[0] ? ": " : "", reason);
    chanout_but(idx, dcc[idx].u.chat->channel, "*** %s.\n", x);
    if (dcc[idx].u.chat->channel < GLOBAL_CHANS)
      botnet_send_part_idx(idx, x);
  }
  check_bind_chof(dcc[idx].nick, idx);
  if ((dcc[idx].sock != STDOUT) || backgrd) {
    killsock(dcc[idx].sock);
    lostdcc(idx);
    /* Entry must remain in the table so it can be logged by the caller */
  } else {
    dprintf(DP_STDOUT, "\n### SIMULATION RESET\n\n");
    dcc_chatter(idx);
  }
  return;
}

int listen_all(int lport, int off)
{
  int i, idx = (-1), port, realport;
#ifdef USE_IPV6
  int i6 = 0;
#endif /* USE_IPV6 */
  struct portmap *pmap = NULL, *pold = NULL;

  port = realport = lport;
  for (pmap = root; pmap; pold = pmap, pmap = pmap->next)
    if (pmap->realport == port) {
      port = pmap->mappedto;
      break;
    }

  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type == &DCC_TELNET) && (dcc[i].port == port)) {
      idx = i;

      if (off) {
        if (pmap) {
          if (pold)
            pold->next = pmap->next;
          else
           root = pmap->next;
           free(pmap);
        }
#ifdef USE_IPV6
        if (sockprotocol(dcc[idx].sock) == AF_INET6)
          putlog(LOG_DEBUG, "*", "Closing IPv6 listening port %d", dcc[idx].port);
        else
#endif /* USE_IPV6 */   
          putlog(LOG_DEBUG, "*", "Closing IPv4 listening port %d", dcc[idx].port);
        killsock(dcc[idx].sock);
        lostdcc(idx);
        return idx;
      }
    }  
  }
  if (idx < 0) {
    if (off) {
      putlog(LOG_ERRORS, "*", STR("No such listening port open - %d"), lport);
      return idx;
    }
    /* make new one */
    if (dcc_total >= max_dcc) {
      putlog(LOG_ERRORS, "*", STR("Can't open listening port - no more DCC Slots"));
    } else {
#ifdef USE_IPV6
      i6 = open_listen_by_af(&port, AF_INET6);
      if (i6 < 0) {
        putlog(LOG_ERRORS, "*", STR("Can't open IPv6 listening port %d - %s"), port, 
               i6 == -1 ? "it's taken." : "couldn't assign ip.");
      } else {
        idx = new_dcc(&DCC_TELNET, 0);
        dcc[idx].addr = 0L;
        strcpy(dcc[idx].addr6, myipstr(6));
        dcc[idx].port = port;
        dcc[idx].sock = i6;
        dcc[idx].timeval = now;
        strcpy(dcc[idx].nick, STR("(telnet6)"));
        strcpy(dcc[idx].host, "*");
        putlog(LOG_DEBUG, "*", STR("Listening on IPv6 at telnet port %d"), port);
      }
      i = open_listen_by_af(&port, AF_INET);
#else
      i = open_listen(&port);
#endif /* USE_IPV6 */
      if (i < 0) {
        putlog(LOG_ERRORS, "*", STR("Can't open IPv4 listening port %d - %s"), port,
               i == -1 ? "it's taken." : "couldn't assign ip.");
      } else {
	idx = (-1); /* now setup ipv4 listening port */
        idx = new_dcc(&DCC_TELNET, 0);
        dcc[idx].addr = iptolong(getmyip());
        dcc[idx].port = port;
        dcc[idx].sock = i;
        dcc[idx].timeval = now;
        strcpy(dcc[idx].nick, STR("(telnet)"));
        strcpy(dcc[idx].host, "*");
        putlog(LOG_DEBUG, "*", STR("Listening on IPv4 at telnet port %d"), port);
      }
#ifdef USE_IPV6
      if (i > 0 || i6 > 0) {
#else
      if (i > 0) {
#endif /* USE_IPV6 */
        if (!pmap) {
          pmap = calloc(1, sizeof(struct portmap));
          pmap->next = root;
          root = pmap;
        }
        pmap->realport = realport;
        pmap->mappedto = port;
      }
    }
  }
  /* if one of the protocols failed, the one which worked will be returned
   * if both were successful, it wont matter which idx is returned, because the 
   * code reading listen_all will only be reading dcc[idx].port, which would be
   * open on both protocols.
   * -lordares (10/29/03)
   */
  return idx;
}
