/*
 * update.c -- part of update.mod
 *
 * $Id$
 */

#define MODULE_NAME "update"
#define MAKING_update

//#ifdef HAVE_UNAME
//#include <sys/utsname.h>
//#endif

#include "src/mod/module.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <pwd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "transfer.mod/transfer.h"
#include "compress.mod/compress.h"

static Function *global = NULL, *transfer_funcs = NULL, *compress_funcs = NULL,
                *uncompress_funcs = NULL;


extern int egg_numver;

/* Prototypes */
static void start_sending_binary(int);
static void cancel_user_xfer(int, void *);

#include "update.h"

#ifdef HUB
int bupdating = 0;
#endif
#ifdef LEAF
int updated = 0;
#endif

/*
 *   Botnet commands
 */

static void update_ufno(int idx, char *par)
{
  putlog(LOG_BOTS, "*", "binary file rejected by %s: %s",
	 dcc[idx].nick, par);
  dcc[idx].status &= ~STAT_OFFEREDU;
}

static void update_ufyes(int idx, char *par)
{
  if (dcc[idx].status & STAT_OFFEREDU) {
    start_sending_binary(idx);
  }
}

static void update_fileq(int idx, char *par)
{
//  int bfl = bot_flags(dcc[idx].user);
  if (dcc[idx].status & STAT_GETTINGU) return;
#ifdef LEAF
  if (updated) return;
  if (localhub) {
#else
  if (!isupdatehub()) {
#endif
    dprintf(idx, "sb uy\n");
  } else if (isupdatehub()) {
    dprintf(idx, "sb un I am the update hub, NOT YOU.\n");
  }
}

/* us <ip> <port> <length>
 */
static void update_ufsend(int idx, char *par)
{

  char *ip=NULL, *port;
  char s[1024];
  int i, sock;
  FILE *f;
  putlog(LOG_BOTS, "*", "Downloading updated binary from %s", dcc[idx].nick);
#ifdef HUB
  egg_snprintf(s, sizeof s, "%s.update.%s.hub", tempdir, botnetnick);
#else
  egg_snprintf(s, sizeof s, "%s.update.%s.leaf", tempdir, botnetnick);
#endif
  unlink(s); //make sure there isnt already a new binary here..
  if (dcc_total == max_dcc) {
    putlog(LOG_MISC, "*", "NO MORE DCC CONNECTIONS -- can't grab new binary");
    dprintf(idx, "sb e I can't open a DCC to you; I'm full.\n");
    zapfbot(idx);
  } else if (!(f = fopen(s, "wb"))) {
    putlog(LOG_MISC, "*", "CAN'T WRITE BINARY DOWNLOAD FILE!");
    zapfbot(idx);
  } else {
    ip = newsplit(&par);
    port = newsplit(&par);
#ifdef USE_IPV6
    sock = getsock(SOCK_BINARY, hostprotocol(ip)); /* Don't buffer this -> mark binary. */
#else
    sock = getsock(SOCK_BINARY); /* Don't buffer this -> mark binary. */
#endif /* USE_IPV6 */
    if (sock < 0 || open_telnet_dcc(sock, ip, port) < 0) {
      killsock(sock);
      putlog(LOG_BOTS, "*", "Asynchronous connection failed!");
      dprintf(idx, "sb e Can't connect to you!\n");
      zapfbot(idx);
    } else {
      putlog(LOG_DEBUG, "*", "Connecting to %s:%s for new binary.", ip, port);
      i = new_dcc(&DCC_FORK_SEND, sizeof(struct xfer_info));
      dcc[i].addr = my_atoul(ip);
      dcc[i].port = atoi(port);
      strcpy(dcc[i].nick, "*binary");
      dcc[i].u.xfer->filename = nmalloc(strlen(s) + 1);
      strcpy(dcc[i].u.xfer->filename, s);
      dcc[i].u.xfer->origname = dcc[i].u.xfer->filename;
      dcc[i].u.xfer->length = atoi(par);
      dcc[i].u.xfer->f = f;
      dcc[i].sock = sock;
      strcpy(dcc[i].host, dcc[idx].nick);

      dcc[idx].status |= STAT_GETTINGU;
    }
  }
}

static void update_version(int idx, char *par)
{
return;
  /* Cleanup any share flags */
#ifdef HUB
  if (bupdating) return;

  dcc[idx].status &= ~(STAT_GETTINGU | STAT_SENDINGU |
                       STAT_OFFEREDU);

  if ((dcc[idx].u.bot->numver < egg_numver)
      && (isupdatehub())) {
    dprintf(idx, "sb u?\n");
    dcc[idx].status |= STAT_OFFEREDU;
  }
#endif
}

/* Note: these MUST be sorted. */
static botcmd_t C_update[] =
{
  {"u?",	(Function) update_fileq},
  {"un",	(Function) update_ufno},
  {"us",	(Function) update_ufsend},
  {"uy",	(Function) update_ufyes},
  {"v",         (Function) update_version},
  {NULL,	NULL}
};

static void got_nu(char *botnick, char *code, char *par)
{
/* needupdate? curver */
  int newver = 0;
#ifdef LEAF
  tand_t *bot;
  struct bot_addr *bi,
   *obi;
  struct userrec *u1;
  bot = tandbot;
  if (!strcmp(bot->bot, botnick)) //dont listen to our uplink.. use normal upate system..
    return;
  if (!localhub)
    return;
  if (localhub && updated)
    return;
#endif
   if (!par[0]) return;
   newver = atoi(newsplit(&par));
   if (newver > egg_numver) {
Context;
#ifdef LEAF
     u1 = get_user_by_handle(userlist, botnetnick);
     obi = get_user(&USERENTRY_BOTADDR, u1);
     bi = user_malloc(sizeof(struct bot_addr));

     bi->uplink = user_malloc(strlen(botnick) + 1);
     strcpy(bi->uplink, botnick);
     bi->address = user_malloc(strlen(obi->address) + 1);
     strcpy(bi->address, obi->address);
     bi->telnet_port = obi->telnet_port;
     bi->relay_port = obi->relay_port;
     bi->hublevel = obi->hublevel;
     set_user(&USERENTRY_BOTADDR, u1, bi);

   /* Change our uplink to them */
//let cont_link restructure us..
     putlog(LOG_MISC, "*", "Changed uplink to %s for update.", botnick);
     botunlink(-2, bot->bot, "Restructure for update.");
     usleep(1000 * 500);
     botlink("", -3, botnick);
#else
     putlog(LOG_MISC, "*", "I need to be updated with %d", newver);
#endif
   }  
}

static cmd_t update_bot[] = {
  {"nu?",    "", (Function) got_nu, NULL}, //need update?
  {0, 0, 0, 0}
};


static void updatein_mod(int idx, char *msg)
{
  char *code;
  int f, i;

  code = newsplit(&msg);
  for (f = 0, i = 0; C_update[i].name && !f; i++) {
    int y = egg_strcasecmp(code, C_update[i].name);

    if (!y)
      /* Found a match */
      (C_update[i].func)(idx, msg);
    if (y < 0)
      f = 1;
  }
}


static void finish_update(int idx)
{
  //module_entry *me;
  struct passwd *pw;
  uid_t id;
  char buf[1024];
  char *buf2;
  int i, j = -1;

  id = geteuid();
  pw = getpwuid(id);

  for (i = 0; i < dcc_total; i++)
    if (!egg_strcasecmp(dcc[i].nick, dcc[idx].host) &&
	(dcc[i].type->flags & DCT_BOT))
      j = i;
  if (j == -1)
    return;

/* NO
  ic = 0;
  next:;
  ic++;
  if (ic > 5) {
    putlog(LOG_MISC, "*", "COULD NOT UNCOMPRESS BINARY");
    return;
  }
  result = 0;
  result = is_compressedfile(dcc[idx].u.xfer->filename);
  if (result == COMPF_COMPRESSED) {
    uncompress_file(dcc[idx].u.xfer->filename);
    usleep(1000 * 500);
    result = is_compressedfile(dcc[idx].u.xfer->filename);
    if (result == COMPF_COMPRESSED)
      goto next;
  }
*/
  sprintf(buf, "%s%s", pw->pw_dir,  strrchr(dcc[idx].u.xfer->filename, '/'));

  movefile(dcc[idx].u.xfer->filename, buf); 
  
  chmod(buf, S_IRUSR | S_IWUSR | S_IXUSR);

  

  sprintf(buf, "%s", strrchr(buf, '/'));
  buf2 = buf;
  buf2++;

  putlog(LOG_MISC, "*", "Updating with binary: %s", buf2);
  
Context;
  if (updatebin(0, buf2, 1))
    putlog(LOG_MISC, "*", "Failed to update to new binary..");
#ifdef LEAF
  else
    updated = 1;
#endif
}

static void start_sending_binary(int idx)
{
  //module_entry *me;
#ifdef HUB
  char update_file[1024];
  char buf2[1024], buf3[1024];
  struct stat sb;
  int i = 1;

  dcc[idx].status &= ~STAT_OFFEREDU;

  if (bupdating) return;
  bupdating = 1;

  dcc[idx].status |= STAT_SENDINGU;

  putlog(LOG_BOTS, "*", "Sending binary send request to %s",dcc[idx].nick);
  if (!strcmp("*", dcc[idx].u.bot->sysname)) {
    putlog(LOG_MISC, "*", "Cannot update \002%s\002 automatically, uname not returning os name.", dcc[idx].nick);
    return;
  }
  if (bot_hublevel(dcc[idx].user) == 999) { //send them the leaf binary..
    sprintf(buf2, "leaf");
  } else {
    sprintf(buf2, "hub");
  }
  sprintf(update_file, "%s.%s.%d", buf2,dcc[idx].u.bot->sysname, egg_numver);

  if (stat(update_file, &sb)) {
    putlog(LOG_MISC, "*", "Need to update \002%s\002 with %s, but it cannot be accessed", dcc[idx].nick, update_file);
    return;
  } 
  sprintf(buf3, "%s.%s", tempdir, update_file);
  unlink(buf3);
  copyfile(update_file, buf3);

/* NO
  ic = 0;
  next:;
  ic++;
  if (ic > 5) {
    putlog(LOG_MISC, "*", "COULD NOT COMPRESS BINARY");
    goto end;
  }
  result = 0;
  result = is_compressedfile(buf3);
  if (result == COMPF_UNCOMPRESSED) {
    compress_file(buf3, 9);
    usleep(1000 * 500);
  }
  result = is_compressedfile(buf3);
  if (result == COMPF_UNCOMPRESSED)
    goto next;
  end:;
*/

  if ((i = raw_dcc_send(buf3, "*binary", "(binary)", buf3)) > 0) {
    putlog(LOG_BOTS, "*", "%s -- can't send new binary",
	   i == DCCSEND_FULL   ? "NO MORE DCC CONNECTIONS" :
	   i == DCCSEND_NOSOCK ? "CAN'T OPEN A LISTENING SOCKET" :
	   i == DCCSEND_BADFN  ? "BAD FILE" :
	   i == DCCSEND_FEMPTY ? "EMPTY FILE" : "UNKNOWN REASON!");
    dcc[idx].status &= ~(STAT_SENDINGU);
  } else {
    dcc[idx].status |= STAT_SENDINGU;
    i = dcc_total - 1;
    strcpy(dcc[i].host, dcc[idx].nick);		/* Store bot's nick */
    dprintf(idx, "sb us %lu %d %lu\n",
	    iptolong(natip[0] ? (IP) inet_addr(natip) : getmyip()),
	    dcc[i].port, dcc[i].u.xfer->length);
  }
#endif
}

static void (*def_dcc_bot_kill) (int, void *) = 0;

static void cancel_user_xfer(int idx, void *x)
{
  int i, j, k = 0;

  if (idx < 0) {
    idx = -idx;
    k = 1;
    updatebot(-1, dcc[idx].nick, '-', 0);
  }
  if (dcc[idx].status & STAT_SHARE) {
    if (dcc[idx].status & STAT_GETTINGU) {
      j = 0;
      for (i = 0; i < dcc_total; i++)
	if (!egg_strcasecmp(dcc[i].host, dcc[idx].nick) &&
	    ((dcc[i].type->flags & (DCT_FILETRAN | DCT_FILESEND)) ==
	     (DCT_FILETRAN | DCT_FILESEND)))
	  j = i;
      if (j != 0) {
	killsock(dcc[j].sock);
	unlink(dcc[j].u.xfer->filename);
	lostdcc(j);
      }
      putlog(LOG_BOTS, "*", "(Userlist download aborted.)");
    }
    if (dcc[idx].status & STAT_SENDINGU) {
      j = 0;
      for (i = 0; i < dcc_total; i++)
	if ((!egg_strcasecmp(dcc[i].host, dcc[idx].nick)) &&
	    ((dcc[i].type->flags & (DCT_FILETRAN | DCT_FILESEND))
	     == DCT_FILETRAN))
	  j = i;
      if (j != 0) {
	killsock(dcc[j].sock);
	unlink(dcc[j].u.xfer->filename);
	lostdcc(j);
      }
      putlog(LOG_BOTS, "*", "(Userlist transmit aborted.)");
    }
  }
  if (!k)
    def_dcc_bot_kill(idx, x);
}

#ifdef HUB
int cnt = 0;
static void check_updates()
{
  int i;
  char buf[1024];

  if (!isupdatehub()) return;
  cnt++;
  if ((cnt > 5) && bupdating)  bupdating = 0; //2 minutes should be plenty.
  if (bupdating) return;
  cnt = 0;
//  tand_t *bot;

  for (i = 0; i < dcc_total; i++) {
    if (dcc[i].type->flags & DCT_BOT && (dcc[i].status & STAT_SHARE) &&
        !(dcc[i].status & STAT_SENDINGU) && !(dcc[i].status & STAT_OFFEREDU) &&
        !(dcc[i].status & STAT_UPDATED)) { //only offer binary to bots that are sharing

      dcc[i].status &= ~(STAT_GETTINGU | STAT_SENDINGU |
                       STAT_OFFEREDU);

      if ((dcc[i].u.bot->numver < egg_numver)
        && (isupdatehub())) {
        dprintf(i, "sb u?\n");
        dcc[i].status |= STAT_OFFEREDU;
      }
    }
  }

  //send out notice to update remote bots ...
  sprintf(buf, "nu? %d", egg_numver);
  botnet_send_zapf_broad(-1, botnetnick, NULL, buf);
}
#endif

static char *update_close()
{
  module_undepend(MODULE_NAME);
  rem_builtins(H_bot, update_bot);
#ifdef HUB
  del_hook(HOOK_30SECONDLY, (Function) check_updates);
#endif
  del_hook(HOOK_SHAREUPDATEIN, (Function) updatein_mod);
  DCC_BOT.kill = def_dcc_bot_kill;
  //uff_deltable(internal_uff_table);
  return NULL;
}

static int update_expmem()
{
  int tot = 0;
  return tot;
}

static void update_report(int idx, int details)
{
  int i, j;

  if (details) {
    dprintf(idx, "    update module, using %d bytes.\n", update_expmem());

    for (i = 0; i < dcc_total; i++)
      if (dcc[i].type == &DCC_BOT) {
	if (dcc[i].status & STAT_GETTINGU) {
	  int ok = 0;

	  for (j = 0; j < dcc_total; j++)
	    if (((dcc[j].type->flags & (DCT_FILETRAN | DCT_FILESEND))
		 == (DCT_FILETRAN | DCT_FILESEND)) &&
		!egg_strcasecmp(dcc[j].host, dcc[i].nick)) {
	      dprintf(idx, "Downloading binary from %s (%d%% done)\n",
		      dcc[i].nick,
		      (int) (100.0 * ((float) dcc[j].status) /
			     ((float) dcc[j].u.xfer->length)));
	      ok = 1;
	      break;
	    }
	  if (!ok)
	    dprintf(idx, "Download binary from %s (negotiating "
		    "botentries)\n", dcc[i].nick);
	} else if (dcc[i].status & STAT_SENDINGU) {
	  for (j = 0; j < dcc_total; j++) {
	    if (((dcc[j].type->flags & (DCT_FILETRAN | DCT_FILESEND))
		 == DCT_FILETRAN)
		&& !egg_strcasecmp(dcc[j].host, dcc[i].nick)) {
	      if (dcc[j].type == &DCC_GET)
		dprintf(idx, "Sending binary to %s (%d%% done)\n",
			dcc[i].nick,
			(int) (100.0 * ((float) dcc[j].status) /
			       ((float) dcc[j].u.xfer->length)));
	      else
		dprintf(idx,
			"Sending binary to %s (waiting for connect)\n",
			dcc[i].nick);
	    }
	  }
	}
      }
  }
}

EXPORT_SCOPE char *update_start();

static Function update_table[] =
{
  /* 0 - 3 */
  (Function) update_start,
  (Function) update_close,
  (Function) update_expmem,
  (Function) update_report,
  /* 4 - 7 */
  (Function) finish_update,
  (Function) 0,
  (Function) 0,
#ifdef LEAF
  (Function) 0
#else
  (Function) 0,
  (Function) & bupdating
#endif
  /* 8 - 11 */
};

char *update_start(Function *global_funcs)
{

  global = global_funcs;

  module_register(MODULE_NAME, update_table, 2, 3);
  if (!(transfer_funcs = module_depend(MODULE_NAME, "transfer", 2, 0))) {
    module_undepend(MODULE_NAME);
    return "This module requires transfer module 2.0 or later.";
  }
  if (!(compress_funcs = module_depend(MODULE_NAME, "compress", 0, 0))) {
    module_undepend(MODULE_NAME);
    return "This module requires the compression.";
  }
  if (!(uncompress_funcs = module_depend(MODULE_NAME, "compress", 0, 0))) {
    module_undepend(MODULE_NAME);
    return "This module requires the compression.";
  }
  add_builtins(H_bot, update_bot);
#ifdef HUB
  add_hook(HOOK_30SECONDLY, (Function) check_updates);
#endif
  add_hook(HOOK_SHAREUPDATEIN, (Function) updatein_mod);
  def_dcc_bot_kill = DCC_BOT.kill;
  DCC_BOT.kill = cancel_user_xfer;
  //uff_init();
  //uff_addtable(internal_uff_table);
  return NULL;
}

