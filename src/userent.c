/*
 * userent.c -- handles:
 *   user-entry handling, new stylem more versatile.
 *
 * $Id$
 */

#include "main.h"
#include "users.h"

extern int		 noshare, cfg_noshare, cfg_count;
extern struct cfg_entry **cfg;
extern struct userrec	*userlist;
extern struct dcc_t	*dcc;
extern Tcl_Interp	*interp;
extern char		 whois_fields[], botnetnick[];
extern time_t            now;

static struct user_entry_type *entry_type_list;


void init_userent()
{
  entry_type_list = 0;
  add_entry_type(&USERENTRY_COMMENT);
  add_entry_type(&USERENTRY_XTRA);
  add_entry_type(&USERENTRY_INFO);
  add_entry_type(&USERENTRY_LASTON);
  add_entry_type(&USERENTRY_BOTADDR);
  add_entry_type(&USERENTRY_PASS);
  add_entry_type(&USERENTRY_SECPASS);
  add_entry_type(&USERENTRY_HOSTS);
  add_entry_type(&USERENTRY_BOTFL);
  add_entry_type(&USERENTRY_STATS);
  add_entry_type(&USERENTRY_ADDED);
  add_entry_type(&USERENTRY_MODIFIED);
  add_entry_type(&USERENTRY_CONFIG);
}

void list_type_kill(struct list_type *t)
{
  struct list_type *u;

  while (t) {
    u = t->next;
    if (t->extra)
      free(t->extra);
    free(t);
    t = u;
  }
}

int def_unpack(struct userrec *u, struct user_entry *e)
{
  char *tmp;

  tmp = e->u.list->extra;
  e->u.list->extra = NULL;
  list_type_kill(e->u.list);
  e->u.string = tmp;
  return 1;
}

int def_pack(struct userrec *u, struct user_entry *e)
{
  char *tmp;

  tmp = e->u.string;
  e->u.list = malloc(sizeof(struct list_type));
  e->u.list->next = NULL;
  e->u.list->extra = tmp;
  return 1;
}

int def_kill(struct user_entry *e)
{
  free(e->u.string);
  free(e);
  return 1;
}

int def_write_userfile(FILE * f, struct userrec *u, struct user_entry *e)
{
  if (lfprintf(f, "--%s %s\n", e->type->name, e->u.string) == EOF)
    return 0;
  return 1;
}

void *def_get(struct userrec *u, struct user_entry *e)
{
  return e->u.string;
}

int def_set(struct userrec *u, struct user_entry *e, void *buf)
{
  char *string = (char *) buf;

  if (string && !string[0])
    string = NULL;
  if (!string && !e->u.string)
    return 1;
  if (string) {
    int l = strlen (string);
    char *i;

    if (l > 160)
      l = 160;


    e->u.string = realloc (e->u.string, l + 1);

    strncpyz (e->u.string, string, l + 1);

    for (i = e->u.string; *i; i++)
      /* Allow bold, inverse, underline, color text here...
       * But never add cr or lf!! --rtc
       */
     if ((unsigned int) *i < 32 && !strchr ("\002\003\026\037", *i))
        *i = '?';
  } else { /* string == NULL && e->u.string != NULL */
    free(e->u.string);
    e->u.string = NULL;
  }
  if (!noshare && !(u->flags & (USER_BOT | USER_UNSHARED))) {
    shareout(NULL, "c %s %s %s\n", e->type->name, u->handle, e->u.string ? e->u.string : "");
  }
  return 1;
}

int def_gotshare(struct userrec *u, struct user_entry *e,
		 char *data, int idx)
{
#ifdef HUB
  putlog(LOG_CMDS, "@", "%s: change %s %s", dcc[idx].nick, e->type->name,
	 u->handle);
#endif
  return e->type->set(u, e, data);
}

int def_tcl_get(Tcl_Interp * interp, struct userrec *u,
		struct user_entry *e, int argc, char **argv)
{
  Tcl_AppendResult(interp, e->u.string, NULL);
  return TCL_OK;
}

int def_tcl_set(Tcl_Interp * irp, struct userrec *u,
		struct user_entry *e, int argc, char **argv)
{
  BADARGS(4, 4, " handle type setting");
  e->type->set(u, e, argv[3]);
  return TCL_OK;
}

void def_display(int idx, struct user_entry *e, struct userrec *u)
{
  dprintf(idx, "  %s: %s\n", e->type->name, e->u.string);
}


int def_dupuser(struct userrec *new, struct userrec *old,
		struct user_entry *e)
{
  return set_user(e->type, new, e->u.string);
}

static void comment_display(int idx, struct user_entry *e, struct userrec *u)
{
  if (dcc[idx].user && (dcc[idx].user->flags & USER_MASTER))
    dprintf(idx, "  COMMENT: %s\n", e->u.string);
}

struct user_entry_type USERENTRY_COMMENT =
{
  0,				/* always 0 ;) */
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  def_tcl_get,
  def_tcl_set,
  comment_display,
  "COMMENT"
};

struct user_entry_type USERENTRY_INFO =
{
  0,				/* always 0 ;) */
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  def_tcl_get,
  def_tcl_set,
  def_display,
  "INFO"
};
void added_display(int idx, struct user_entry *e, struct userrec *u)
{
  /* format: unixtime handle */
  if (dcc[idx].user && (dcc[idx].user->flags & USER_OWNER)) {
    char tmp[30],
      tmp2[70],
     *hnd;
    time_t tm;

    strncpyz(tmp, e->u.string, sizeof(tmp));
    hnd = strchr(tmp, ' ');
    if (hnd)
      *hnd++ = 0;
    tm = atoi(tmp);

#ifdef S_UTCTIME
    egg_strftime(tmp2, sizeof(tmp2), STR("%a, %d %b %Y %H:%M:%S %Z"), gmtime(&tm));
#else /* !S_UTCTIME */
    egg_strftime(tmp2, sizeof(tmp2), STR("%a, %d %b %Y %H:%M:%S %Z"), localtime(&tm));
#endif /* S_UTCTIME */
    if (hnd)
      dprintf(idx, STR("  -- Added %s by %s\n"), tmp2, hnd);
    else
      dprintf(idx, STR("  -- Added %s\n"), tmp2);
  }
}

struct user_entry_type USERENTRY_ADDED = {
  0,				/* always 0 ;) */
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  0,
  0,
  added_display,
  "ADDED"
};

int config_set(struct userrec *u, struct user_entry *e, void *buf)
{
  struct xtra_key *curr, *old = NULL, *new = buf;

  for (curr = e->u.extra; curr; curr = curr->next) {
    if (curr->key && !egg_strcasecmp(curr->key, new->key)) {
      old = curr;
      break;
    }
  }
  if (!old && (!new->data || !new->data[0])) {
    /* delete non-existant entry */
    free(new->key);
    if (new->data)
      free(new->data);
    free(new);
    return 1;
  }

  /* we will possibly free new below, so let's send the information
   * to the botnet now */
  if (!noshare && !cfg_noshare)
    shareout(NULL, STR("c CONFIG %s %s %s\n"), u->handle, new->key, new->data ? new->data : "");
  if ((old && old != new) || !new->data || !new->data[0]) {
    list_delete((struct list_type **) (&e->u.extra), (struct list_type *) old);

    free(old->key);
    free(old->data);
    free(old);
  }
  if (old != new && new->data) {
    if (new->data[0]) {
      list_insert((&e->u.extra), new);
    } else {
      if (new->data)
        free(new->data);
      free(new->key);
      free(new);
    }
  }
  return 1;
}

int config_unpack(struct userrec *u, struct user_entry *e)
{
  struct list_type *curr,
   *head;
  struct xtra_key *t;
  char *key,
   *data;

  head = curr = e->u.list;
  e->u.extra = NULL;
  while (curr) {
    t = malloc(sizeof(struct xtra_key));

    data = curr->extra;
    key = newsplit(&data);
    if (data[0]) {
      t->key = malloc(strlen(key) + 1);
      strcpy(t->key, key);
      t->data = malloc(strlen(data) + 1);
      strcpy(t->data, data);
      list_insert((&e->u.extra), t);
    }
    curr = curr->next;
  }
  list_type_kill(head);
  return 1;
}

int config_pack(struct userrec *u, struct user_entry *e)
{
  struct list_type *t;
  struct xtra_key *curr,
   *next;

  curr = e->u.extra;
  e->u.list = NULL;
  while (curr) {
    t = malloc(sizeof(struct list_type));

    t->extra = malloc(strlen(curr->key) + strlen(curr->data) + 4);
    sprintf(t->extra, STR("%s %s"), curr->key, curr->data);
    list_insert((&e->u.list), t);
    next = curr->next;
    free(curr->key);
    free(curr->data);
    free(curr);
    curr = next;
  }
  return 1;
}

void config_display(int idx, struct user_entry *e, struct userrec *u)
{
#ifdef HUB
  struct xtra_key *xk;
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  get_user_flagrec(dcc[idx].user, &fr, NULL);
  /* scan thru xtra field, searching for matches */
  for (xk = e->u.extra; xk; xk = xk->next) {
    /* ok, it's a valid xtra field entry */
    if (glob_owner(fr))
      dprintf(idx, STR("  %s: %s\n"), xk->key, xk->data);
  }
#endif /* HUB */
}

int config_gotshare(struct userrec *u, struct user_entry *e, char *buf, int idx)
{
  char *arg;
  struct xtra_key *xk;
  int l;

  arg = newsplit(&buf);
  if (!arg[0])
    return 1;
  if (!strcmp(u->handle, botnetnick)) {
    struct cfg_entry *cfgent = NULL;
    int i;

    cfg_noshare = 1;
    for (i = 0; !cfgent && (i < cfg_count); i++)
      if (!strcmp(arg, cfg[i]->name) && (cfg[i]->flags & CFGF_LOCAL))
	cfgent = cfg[i];
    if (cfgent) {
      set_cfg_str(botnetnick, cfgent->name, (buf && buf[0]) ? buf : NULL);
    }
    cfg_noshare = 0;
    return 1;
  }

  xk = malloc(sizeof(struct xtra_key));
  egg_bzero(xk, sizeof(struct xtra_key));

  l = strlen(arg);
  if (l > 1500)
    l = 1500;
  xk->key = malloc(l + 1);
  strncpyz(xk->key, arg, l + 1);

  if (buf && buf[0]) {
    int k = strlen(buf);

    if (k > 1500 - l)
      k = 1500 - l;
    xk->data = malloc(k + 1);
    strncpyz(xk->data, buf, k + 1);
  }
  config_set(u, e, xk);

  return 1;
}

int config_dupuser(struct userrec *new, struct userrec *old, struct user_entry *e)
{
  struct xtra_key *x1,
   *x2;

  for (x1 = e->u.extra; x1; x1 = x1->next) {
    x2 = malloc(sizeof(struct xtra_key));

    x2->key = malloc(strlen(x1->key) + 1);
    strcpy(x2->key, x1->key);
    x2->data = malloc(strlen(x1->data) + 1);
    strcpy(x2->data, x1->data);
    set_user(&USERENTRY_CONFIG, new, x2);
  }
  return 1;
}

int config_write_userfile(FILE *f, struct userrec *u, struct user_entry *e)
{
  struct xtra_key *x;

  for (x = e->u.extra; x; x = x->next)
    lfprintf(f, STR("--CONFIG %s %s\n"), x->key, x->data);
  return 1;
}

int config_kill(struct user_entry *e)
{
  struct xtra_key *x,
   *y;

  for (x = e->u.extra; x; x = y) {
    y = x->next;
    free(x->key);
    free(x->data);
    free(x);
  }
  free(e);
  return 1;
}

struct user_entry_type USERENTRY_CONFIG = {
  0,
  config_gotshare,
  config_dupuser,
  config_unpack,
  config_pack,
  config_write_userfile,
  config_kill,
  def_get,
  config_set,
  0,
  0,
  config_display,
  "CONFIG"
};

void stats_add(struct userrec *u, int login, int op)
{
  char *s,
    s2[50];
  int sl,
    so;

  if (!u)
    return;
  s = get_user(&USERENTRY_STATS, u);
  if (s) {
    strncpyz(s2, s, sizeof(s2));
  } else
    strcpy(s2, STR("0 0"));
  s = strchr(s2, ' ');
  if (s) {
    s++;
    so = atoi(s);
  } else
    so = 0;
  sl = atoi(s2);
  if (login)
    sl++;
  if (op)
    so++;
  sprintf(s2, STR("%i %i"), sl, so);
  set_user(&USERENTRY_STATS, u, s2);
}

void stats_display(int idx, struct user_entry *e, struct userrec *u)
{
  /* format: logincount opcount */
  if (dcc[idx].user && (dcc[idx].user->flags & USER_OWNER)) {
    char *p;

    p = strchr(e->u.string, ' ');
    if (p)
      dprintf(idx, STR("  -- %i logins, %i ops\n"), atoi(e->u.string), atoi(p));
  }
}

struct user_entry_type USERENTRY_STATS = {
  0,				/* always 0 ;) */
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  0,
  0,
  stats_display,
  "STATS"
};

void update_mod(char *handle, char *nick, char *cmd, char *par)
{
  char tmp[100];
  egg_snprintf(tmp, sizeof tmp, "%lu, %s (%s %s)", now, nick, cmd, (par && par[0]) ? par : "");
  set_user(&USERENTRY_MODIFIED, get_user_by_handle(userlist, handle), tmp);
}

void modified_display(int idx, struct user_entry *e, struct userrec *u)
{
  if (e && dcc[idx].user && (dcc[idx].user->flags & USER_MASTER)) {
    char tmp[1024],
      tmp2[1024],
     *hnd;
    time_t tm;
    strncpyz(tmp, e->u.string, sizeof(tmp));
    hnd = strchr(tmp, ' ');
    if (hnd)
      *hnd++ = 0;
    tm = atoi(tmp);
#ifdef S_UTCTIME
    egg_strftime(tmp2, sizeof(tmp2), STR("%a, %d %b %Y %H:%M:%S %Z"), gmtime(&tm));
#else /* !S_UTCTIME */
    egg_strftime(tmp2, sizeof(tmp2), STR("%a, %d %b %Y %H:%M:%S %Z"), localtime(&tm));
#endif /* S_UTCTIME */
    if (hnd)
      dprintf(idx, "  -- Modified %s by %s\n", tmp2, hnd);
    else
      dprintf(idx, "  -- Modified %s\n", tmp2);
  }
}

struct user_entry_type USERENTRY_MODIFIED =
{
  0,
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  0,
  0,
  modified_display,
  "MODIFIED"
};

int pass_set(struct userrec *u, struct user_entry *e, void *buf)
{
  char new[32];
  register char *pass = buf;

  if (e->u.extra)
    free(e->u.extra);
  if (!pass || !pass[0] || (pass[0] == '-'))
    e->u.extra = NULL;
  else {
    unsigned char *p = (unsigned char *) pass;

    if (strlen(pass) > 15)
      pass[15] = 0;
    while (*p) {
      if ((*p <= 32) || (*p == 127))
	*p = '?';
      p++;
    }
    if ((u->flags & USER_BOT) || (pass[0] == '+'))
      strcpy(new, pass);
    else
      encrypt_pass(pass, new);
    e->u.extra = malloc(strlen(new) + 1);
    strcpy(e->u.extra, new);
  }
  if (!noshare && !(u->flags & (USER_BOT | USER_UNSHARED)))
    shareout(NULL, "c PASS %s %s\n", u->handle, pass ? pass : "");
  return 1;
}

static int pass_tcl_set(Tcl_Interp * irp, struct userrec *u,
			struct user_entry *e, int argc, char **argv)
{
  BADARGS(3, 4, " handle PASS ?newpass?");
  pass_set(u, e, argc == 3 ? NULL : argv[3]);
  return TCL_OK;
}

struct user_entry_type USERENTRY_PASS =
{
  0,
  def_gotshare,
  0,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  pass_set,
  def_tcl_get,
  pass_tcl_set,
  0,
  "PASS"
};


void secpass_display(int idx, struct user_entry *e, struct userrec *u)
{
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  get_user_flagrec(dcc[idx].user, &fr, NULL);

  if (!strcmp(u->handle, dcc[idx].nick) || glob_admin(fr)) {
#ifdef HUB
    dprintf(idx, "  %s: %s\n", e->type->name, e->u.string);
#else
    dprintf(idx, "  %s: Hidden on leaf bots.", e->type->name);
    if (dcc[idx].u.chat->su_nick)
      dprintf(idx, " Nice try, %s.", dcc[idx].u.chat->su_nick);
    dprintf(idx, "\n");
#endif /* HUB */
  }
}


struct user_entry_type USERENTRY_SECPASS =
{
  0,
  def_gotshare,
  def_dupuser,
  def_unpack,
  def_pack,
  def_write_userfile,
  def_kill,
  def_get,
  def_set,
  0,
  0,
  secpass_display,
  "SECPASS"
};

static int laston_unpack(struct userrec *u, struct user_entry *e)
{
  char *par, *arg;
  struct laston_info *li;

  par = e->u.list->extra;
  arg = newsplit (&par);
  if (!par[0])
    par = "???";
  li = malloc(sizeof(struct laston_info));
  li->lastonplace = malloc(strlen(par) + 1);
  li->laston = atoi(arg);
  strcpy(li->lastonplace, par);
  list_type_kill(e->u.list);
  e->u.extra = li;
  return 1;
}

static int laston_pack(struct userrec *u, struct user_entry *e)
{
  char work[1024];
  struct laston_info *li;
  int l;

  li = (struct laston_info *) e->u.extra;
  l = sprintf(work, "%lu %s", li->laston, li->lastonplace);
  e->u.list = malloc(sizeof(struct list_type));
  e->u.list->next = NULL;
  e->u.list->extra = malloc(l + 1);
  strcpy(e->u.list->extra, work);
  free(li->lastonplace);
  free(li);
  return 1;
}

static int laston_write_userfile(FILE * f,
				 struct userrec *u,
				 struct user_entry *e)
{
  struct laston_info *li = (struct laston_info *) e->u.extra;

  if (lfprintf(f, "--LASTON %lu %s\n", li->laston,
	      li->lastonplace ? li->lastonplace : "") == EOF)
    return 0;
  return 1;
}

static int laston_kill(struct user_entry *e)
{
  if (((struct laston_info *) (e->u.extra))->lastonplace)
    free(((struct laston_info *) (e->u.extra))->lastonplace);
  free(e->u.extra);
  free(e);
  return 1;
}

static int laston_set(struct userrec *u, struct user_entry *e, void *buf)
{
  struct laston_info *li = (struct laston_info *) e->u.extra;

  if (li != buf) {
    if (li) {
      free(li->lastonplace);
      free(li);
    }

    li = e->u.extra = buf;
  }
  /* donut share laston info */
  return 1;
}

static int laston_tcl_get(Tcl_Interp * irp, struct userrec *u,
			  struct user_entry *e, int argc, char **argv)
{
  struct laston_info *li = (struct laston_info *) e->u.extra;
  char number[20];
  struct chanuserrec *cr;

  BADARGS(3, 4, " handle LASTON ?channel?");
  if (argc == 4) {
    for (cr = u->chanrec; cr; cr = cr->next)
      if (!rfc_casecmp(cr->channel, argv[3])) {
	Tcl_AppendResult(irp, int_to_base10(cr->laston), NULL);
	break;
      }
    if (!cr)
      Tcl_AppendResult(irp, "0", NULL);
  } else {
    sprintf(number, "%lu ", li->laston);
    Tcl_AppendResult(irp, number, li->lastonplace, NULL);
  }
  return TCL_OK;
}

static int laston_tcl_set(Tcl_Interp * irp, struct userrec *u,
			  struct user_entry *e, int argc, char **argv)
{
  struct laston_info *li;
  struct chanuserrec *cr;

  BADARGS(4, 5, " handle LASTON time ?place?");

  if ((argc == 5) && argv[4][0] && strchr(CHANMETA, argv[4][0])) {
    /* Search for matching channel */
    for (cr = u->chanrec; cr; cr = cr->next)
      if (!rfc_casecmp(cr->channel, argv[4])) {
	cr->laston = atoi(argv[3]);
	break;
      }
  }
  /* Save globally */
  li = malloc(sizeof(struct laston_info));

  if (argc == 5) {
    li->lastonplace = malloc(strlen(argv[4]) + 1);
    strcpy(li->lastonplace, argv[4]);
  } else {
    li->lastonplace = malloc(1);
    li->lastonplace[0] = 0;
  }
  li->laston = atoi(argv[3]);
  set_user(&USERENTRY_LASTON, u, li);
  return TCL_OK;
}

static int laston_dupuser(struct userrec *new, struct userrec *old,
			  struct user_entry *e)
{
  struct laston_info *li = e->u.extra, *li2;

  if (li) {
    li2 = malloc(sizeof(struct laston_info));

    li2->laston = li->laston;
    li2->lastonplace = malloc(strlen(li->lastonplace) + 1);
    strcpy(li2->lastonplace, li->lastonplace);
    return set_user(&USERENTRY_LASTON, new, li2);
  }
  return 0;
}

struct user_entry_type USERENTRY_LASTON =
{
  0,				/* always 0 ;) */
  0,
  laston_dupuser,
  laston_unpack,
  laston_pack,
  laston_write_userfile,
  laston_kill,
  def_get,
  laston_set,
  laston_tcl_get,
  laston_tcl_set,
  0,
  "LASTON"
};

static int botaddr_unpack(struct userrec *u, struct user_entry *e)
{
  char p[1024],
   *q1,
   *q2;
  struct bot_addr *bi = malloc(sizeof(struct bot_addr));
  egg_bzero(bi, sizeof(struct bot_addr));

  /* address:port/port:hublevel:uplink */
  Context;
  Assert(e);
  Assert(e->name);
  egg_bzero(bi, sizeof(struct bot_addr));

  strcpy(p, e->u.list->extra);
  q1 = strchr(p, ':');
  if (q1)
    *q1++ = 0;
  bi->address = malloc(strlen(p) + 1);
  strcpy(bi->address, p);
  if (q1) {
    q2 = strchr(q1, ':');
    if (q2)
      *q2++ = 0;
    bi->telnet_port = atoi(q1);
    q1 = strchr(q1, '/');
    if (q1) {
      q1++;
      bi->relay_port = atoi(q1);
    }
    if (q2) {
      q1 = strchr(q2, ':');
      if (q1) {
        *q1++ = 0;
        bi->uplink = malloc(strlen(q1) + 1);
        strcpy(bi->uplink, q1);

      }
      bi->hublevel = atoi(q2);
    }
  }
  if (!bi->telnet_port)
    bi->telnet_port = 3333;
  if (!bi->relay_port)
    bi->relay_port = bi->telnet_port;
  if (!bi->uplink) {
    bi->uplink = malloc(1);
    bi->uplink[0] = 0;
  }
  list_type_kill(e->u.list);
  e->u.extra = bi;
  return 1;

}
static int botaddr_pack(struct userrec *u, struct user_entry *e)
{
  char work[1024];
  struct bot_addr *bi;
  int l;

  Assert(e);
  Assert(!e->name);
  bi = (struct bot_addr *) e->u.extra;
  l = simple_sprintf(work, STR("%s:%u/%u:%u:%s"), bi->address, bi->telnet_port, bi->relay_port, bi->hublevel, bi->uplink);
  e->u.list = malloc(sizeof(struct list_type));

  e->u.list->next = NULL;
  e->u.list->extra = malloc(l + 1);
  strcpy(e->u.list->extra, work);
  free(bi->address);
  free(bi->uplink);
  free(bi);
  return 1;
}

static int botaddr_kill(struct user_entry *e)
{
  free(((struct bot_addr *) (e->u.extra))->address);
  free(((struct bot_addr *) (e->u.extra))->uplink);
  free(e->u.extra);
  free(e);
  return 1;
}

static int botaddr_write_userfile(FILE *f, struct userrec *u,
				  struct user_entry *e)
{
  register struct bot_addr *bi = (struct bot_addr *) e->u.extra;


  if (lfprintf(f,  "--%s %s:%u/%u:%u:%s\n", e->type->name, bi->address,
	      bi->telnet_port, bi->relay_port, bi->hublevel, bi->uplink) == EOF)
    return 0;
  return 1;
}

static int botaddr_set(struct userrec *u, struct user_entry *e, void *buf)
{
  register struct bot_addr *bi = (struct bot_addr *) e->u.extra;

  Context;
  if (!bi && !buf)
    return 1;
  if (bi != buf) {
    if (bi) {
      Assert(bi->address);
      free(bi->address);
      Assert(bi->uplink);
      free(bi->uplink);
      free(bi);
    }
    ContextNote("(sharebug) occurred in botaddr_set");
    bi = e->u.extra = buf;
  }
  Assert(u);
  if (bi && !noshare && !(u->flags & USER_UNSHARED)) {
    shareout(NULL, STR("c BOTADDR %s %s %d %d %d %s\n"),u->handle, (bi->address && bi->address[0]) ? bi->address : STR("127.0.0.1"), bi->telnet_port, bi->relay_port, bi->hublevel, bi->uplink);
  }
  return 1;
}

static int botaddr_tcl_get(Tcl_Interp *interp, struct userrec *u,
			   struct user_entry *e, int argc, char **argv)
{
  register struct bot_addr *bi = (struct bot_addr *) e->u.extra;
  char number[20];

  Context;
  sprintf(number, STR(" %d"), bi->telnet_port);
  Tcl_AppendResult(interp, bi->address, number, NULL);
  sprintf(number, STR(" %d"), bi->relay_port);
  Tcl_AppendResult(interp, number, NULL);
  sprintf(number, STR(" %d"), bi->hublevel);
  Tcl_AppendResult(interp, number, NULL);
  Tcl_AppendResult(interp, bi->uplink, NULL);
  return TCL_OK;
}

static int botaddr_tcl_set(Tcl_Interp *irp, struct userrec *u,
			   struct user_entry *e, int argc, char **argv)
{
  register struct bot_addr *bi = (struct bot_addr *) e->u.extra;

  BADARGS(4, 6, " handle type address ?telnetport ?relayport??");
  if (u->flags & USER_BOT) {
    /* Silently ignore for users */
    if (!bi) {
      bi = malloc(sizeof(struct bot_addr));
      egg_bzero(bi, sizeof (struct bot_addr));
    } else {
      free(bi->address);
    }
    bi->address = malloc(strlen(argv[3]) + 1);
    strcpy(bi->address, argv[3]);
    if (argc > 4)
      bi->telnet_port = atoi(argv[4]);
    if (argc > 5)
      bi->relay_port = atoi(argv[5]);
    if (!bi->telnet_port)
      bi->telnet_port = 3333;
    if (!bi->relay_port)
      bi->relay_port = bi->telnet_port;
    botaddr_set(u, e, bi);
  }
  return TCL_OK;
}

static void botaddr_display(int idx, struct user_entry *e, struct userrec *u)
{
#ifdef HUB
  struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

  get_user_flagrec(dcc[idx].user, &fr, NULL);
  if (glob_admin(fr)) {
    register struct bot_addr *bi = (struct bot_addr *) e->u.extra;
    if (bi->address && bi->hublevel && bi->hublevel != 0)
      dprintf(idx, STR("  ADDRESS: %.70s:%d\n"), bi->address, bi->telnet_port);
    if (bi->hublevel && bi->hublevel != 0)
      dprintf(idx, STR("  HUBLEVEL: %d\n"), bi->hublevel);
    if (bi->uplink && bi->uplink[0])
      dprintf(idx, STR("  UPLINK: %s\n"), bi->uplink);
  }
#endif /* HUB */
}

static int botaddr_gotshare(struct userrec *u, struct user_entry *e,
			    char *buf, int idx)
{
  struct bot_addr *bi = malloc(sizeof(struct bot_addr));
  char *arg;

  egg_bzero(bi, sizeof(struct bot_addr));

  arg = newsplit(&buf);
  bi->address = malloc(strlen(arg) + 1);
  strcpy(bi->address, arg);
  arg = newsplit(&buf);
  bi->telnet_port = atoi(arg);
  arg = newsplit(&buf);
  bi->relay_port = atoi(arg);
  arg = newsplit(&buf);
  bi->hublevel = atoi(arg);
  bi->uplink = malloc(strlen(buf) + 1);
  strcpy(bi->uplink, buf);
  if (!bi->telnet_port)
    bi->telnet_port = 3333;
  if (!bi->relay_port)
    bi->relay_port = bi->telnet_port;
  return botaddr_set(u, e, bi);
}

static int botaddr_dupuser(struct userrec *new, struct userrec *old,
			   struct user_entry *e)
{
  if (old->flags & USER_BOT) {
    struct bot_addr *bi = e->u.extra,
     *bi2;

    if (bi) {
      bi2 = malloc(sizeof(struct bot_addr));

      bi2->telnet_port = bi->telnet_port;
      bi2->relay_port = bi->relay_port;
      bi2->hublevel = bi->hublevel;
      bi2->address = malloc(strlen(bi->address) + 1);
      bi2->uplink = malloc(strlen(bi->uplink) + 1);
      strcpy(bi2->address, bi->address);
      strcpy(bi2->uplink, bi->uplink);
      return set_user(&USERENTRY_BOTADDR, new, bi2);
    }
  }
  return 0;
}

struct user_entry_type USERENTRY_BOTADDR =
{
  0,				/* always 0 ;) */
  botaddr_gotshare,
  botaddr_dupuser,
  botaddr_unpack,
  botaddr_pack,
  botaddr_write_userfile,
  botaddr_kill,
  def_get,
  botaddr_set,
  botaddr_tcl_get,
  botaddr_tcl_set,
  botaddr_display,
  "BOTADDR"
};
int xtra_set(struct userrec *u, struct user_entry *e, void *buf)
{
  struct xtra_key *curr, *old = NULL, *new = buf;

  for (curr = e->u.extra; curr; curr = curr->next) {
    if (curr->key && !egg_strcasecmp(curr->key, new->key)) {
      old = curr;
      break;
    }
  }
  if (!old && (!new->data || !new->data[0])) {
    /* Delete non-existant entry -- doh ++rtc */
    free(new->key);
    if (new->data)
      free(new->data);
    free(new);
    return TCL_OK;
  }

  /* We will possibly free new below, so let's send the information
   * to the botnet now
   */
  if (!noshare && !(u->flags & (USER_BOT | USER_UNSHARED)))
    shareout(NULL, "c XTRA %s %s %s\n", u->handle, new->key,
             new->data ? new->data : "");
  if ((old && old != new) || !new->data || !new->data[0]) {
    list_delete((struct list_type **) (&e->u.extra),
                (struct list_type *) old);
    free(old->key);
    free(old->data);
    free(old);
  }
  if (old != new && new->data) {
    if (new->data[0])
      list_insert((&e->u.extra), new) /* do not add a ';' here */
  } else {
    if (new->data)
      free(new->data);
    free(new->key);
    free(new);
  }
  return TCL_OK;
}
static int xtra_tcl_set(Tcl_Interp * irp, struct userrec *u,
                        struct user_entry *e, int argc, char **argv)
{
  struct xtra_key *xk;
  int l;

  BADARGS(4, 5, " handle type key ?value?");
  xk = malloc(sizeof(struct xtra_key));
  l = strlen(argv[3]);
  egg_bzero(xk, sizeof (struct xtra_key));
  if (l > 500)
    l = 500;
  xk->key = malloc(l + 1);
  strncpyz(xk->key, argv[3], l + 1);

  if (argc == 5) {
    int k = strlen(argv[4]);

    if (k > 500 - l)
      k = 500 - l;
    xk->data = malloc(k + 1);
    strncpyz(xk->data, argv[4], k + 1);
  }
  xtra_set(u, e, xk);
  return TCL_OK;
}

int xtra_unpack(struct userrec *u, struct user_entry *e)
{
  struct list_type *curr, *head;
  struct xtra_key *t;
  char *key, *data;

  head = curr = e->u.list;
  e->u.extra = NULL;
  while (curr) {
    t = malloc(sizeof(struct xtra_key));

    data = curr->extra;
    key = newsplit(&data);
    if (data[0]) {
      t->key = malloc(strlen(key) + 1);
      strcpy(t->key, key);
      t->data = malloc(strlen(data) + 1);
      strcpy(t->data, data);
      list_insert((&e->u.extra), t);
    }
    curr = curr->next;
  }
  list_type_kill(head);
  return 1;
}

static int xtra_pack(struct userrec *u, struct user_entry *e)
{
  struct list_type *t;
  struct xtra_key *curr, *next;

  curr = e->u.extra;
  e->u.list = NULL;
  while (curr) {
    t = malloc(sizeof(struct list_type));
    t->extra = malloc(strlen(curr->key) + strlen(curr->data) + 4);
    sprintf(t->extra, "%s %s", curr->key, curr->data);
    list_insert((&e->u.list), t);
    next = curr->next;
    free(curr->key);
    free(curr->data);
    free(curr);
    curr = next;
  }
  return 1;
}

static void xtra_display(int idx, struct user_entry *e, struct userrec *u)
{
  int code, lc, j;
  struct xtra_key *xk;
#if ((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4))
  CONST char **list;
#else
  char **list;
#endif
  code = Tcl_SplitList(interp, whois_fields, &lc, &list);
  if (code == TCL_ERROR)
    return;
  /* Scan thru xtra field, searching for matches */
  for (xk = e->u.extra; xk; xk = xk->next) {
    /* Ok, it's a valid xtra field entry */
    for (j = 0; j < lc; j++) {
      if (!egg_strcasecmp(list[j], xk->key))
        dprintf(idx, "  %s: %s\n", xk->key, xk->data);
    }
  }
  Tcl_Free((char *) list);
}

static int xtra_gotshare(struct userrec *u, struct user_entry *e,
                         char *buf, int idx)
{
  char *arg;
  struct xtra_key *xk;
  int l;
  arg = newsplit (&buf);
  if (!arg[0])
    return 1;

  xk = malloc (sizeof(struct xtra_key));
  egg_bzero(xk, sizeof(struct xtra_key));
  l = strlen(arg);
  if (l > 500)
    l = 500;
  xk->key = malloc(l + 1);
  strncpyz(xk->key, arg, l + 1);

  if (buf[0]) {
    int k = strlen(buf);

    if (k > 500 - l)
      k = 500 - l;
    xk->data = malloc(k + 1);
    strncpyz(xk->data, buf, k + 1);
  }
  xtra_set(u, e, xk);
  return 1;
}

static int xtra_dupuser(struct userrec *new, struct userrec *old,
                        struct user_entry *e)
{
  struct xtra_key *x1, *x2;

  for (x1 = e->u.extra; x1; x1 = x1->next) {
    x2 = malloc(sizeof(struct xtra_key));

    x2->key = malloc(strlen(x1->key) + 1);
    strcpy(x2->key, x1->key);
    x2->data = malloc(strlen(x1->data) + 1);
    strcpy(x2->data, x1->data);
    set_user(&USERENTRY_XTRA, new, x2);
  }
  return 1;
}
static int xtra_write_userfile(FILE *f, struct userrec *u, struct user_entry *e)
{
  struct xtra_key *x;

  for (x = e->u.extra; x; x = x->next)
    if (lfprintf(f, "--XTRA %s %s\n", x->key, x->data) == EOF)
      return 0;
  return 1;
}

int xtra_kill(struct user_entry *e)
{
  struct xtra_key *x, *y;

  for (x = e->u.extra; x; x = y) {
    y = x->next;
    free(x->key);
    free(x->data);
    free(x);
  }
  free(e);
  return 1;
}
static int xtra_tcl_get(Tcl_Interp *irp, struct userrec *u,
                        struct user_entry *e, int argc, char **argv)
{
  struct xtra_key *x;

  BADARGS(3, 4, " handle XTRA ?key?");
  if (argc == 4) {
    for (x = e->u.extra; x; x = x->next)
      if (!egg_strcasecmp(argv[3], x->key)) {
        Tcl_AppendResult(irp, x->data, NULL);
        return TCL_OK;
      }
    return TCL_OK;
  }
  for (x = e->u.extra; x; x = x->next) {
    char *p;
#if ((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4))
    CONST char *list[2];
#else
    char *list[2];
#endif

    list[0] = x->key;
    list[1] = x->data;
    p = Tcl_Merge(2, list);
    Tcl_AppendElement(irp, p);
    Tcl_Free((char *) p);
  }
  return TCL_OK;
}

struct user_entry_type USERENTRY_XTRA =
{
  0,
  xtra_gotshare,
  xtra_dupuser,
  xtra_unpack,
  xtra_pack,
  xtra_write_userfile,
  xtra_kill,
  def_get,
  xtra_set,
  xtra_tcl_get,
  xtra_tcl_set,
  xtra_display,
  "XTRA"
};


static int hosts_dupuser(struct userrec *new, struct userrec *old,
			 struct user_entry *e)
{
  struct list_type *h;

  for (h = e->u.extra; h; h = h->next)
    set_user(&USERENTRY_HOSTS, new, h->extra);
  return 1;
}

static int hosts_null(struct userrec *u, struct user_entry *e)
{
  return 1;
}

static int hosts_write_userfile(FILE *f, struct userrec *u, struct user_entry *e)
{
  struct list_type *h;

  for (h = e->u.extra; h; h = h->next)
    if (lfprintf(f, "--HOSTS %s\n", h->extra) == EOF)
      return 0;
  return 1;
}

static int hosts_kill(struct user_entry *e)
{
  list_type_kill(e->u.list);
  free(e);
  return 1;
}

static void hosts_display(int idx, struct user_entry *e, struct userrec *u)
{
#ifdef LEAF
  /* if this is a su, dont show hosts
   * otherwise, let users see their own hosts */
  if (!strcmp(u->handle,dcc[idx].nick) && !dcc[idx].u.chat->su_nick) { 
#endif /* LEAF */
    char s[1024];
    struct list_type *q;

    s[0] = 0;
    strcpy(s, "  HOSTS: ");
    for (q = e->u.list; q; q = q->next) {
      if (s[0] && !s[9])
        strcat(s, q->extra);
      else if (!s[0])
        sprintf(s, "         %s", q->extra);
      else {
        if (strlen(s) + strlen(q->extra) + 2 > 65) {
  	  dprintf(idx, "%s\n", s);
  	  sprintf(s, "         %s", q->extra);
        } else {
  	  strcat(s, ", ");
  	  strcat(s, q->extra);
        }
      }
    }
    if (s[0])
      dprintf(idx, "%s\n", s);
#ifdef LEAF
  } else {
    dprintf(idx, "  HOSTS:          Hidden on leaf bots.");
    if (dcc[idx].u.chat->su_nick)
      dprintf(idx, " Nice try, %s.", dcc[idx].u.chat->su_nick);
    dprintf(idx, "\n");
  }
#endif /* LEAF */
}

static int hosts_set(struct userrec *u, struct user_entry *e, void *buf)
{
  if (!buf || !egg_strcasecmp(buf, "none")) {
    /* When the bot crashes, it's in this part, not in the 'else' part */
    list_type_kill(e->u.list);
    e->u.list = NULL;
  } else {
    char *host = buf, *p = strchr(host, ',');
    struct list_type **t;

    /* Can't have ,'s in hostmasks */
    while (p) {
      *p = '?';
      p = strchr(host, ',');
    }
    /* fred1: check for redundant hostmasks with
     * controversial "superpenis" algorithm ;) */
    /* I'm surprised Raistlin hasn't gotten involved in this controversy */
    t = &(e->u.list);
    while (*t) {
      if (wild_match(host, (*t)->extra)) {
	struct list_type *u;

	u = *t;
	*t = (*t)->next;
	if (u->extra)
	  free(u->extra);
	free(u);
      } else
	t = &((*t)->next);
    }
    *t = malloc(sizeof(struct list_type));

    (*t)->next = NULL;
    (*t)->extra = malloc(strlen(host) + 1);
    strcpy((*t)->extra, host);
  }
  return 1;
}

static int hosts_tcl_get(Tcl_Interp *irp, struct userrec *u,
			 struct user_entry *e, int argc, char **argv)
{
  struct list_type *x;

  BADARGS(3, 3, " handle HOSTS");
  for (x = e->u.list; x; x = x->next)
    Tcl_AppendElement(irp, x->extra);
  return TCL_OK;
}

static int hosts_tcl_set(Tcl_Interp * irp, struct userrec *u,
			 struct user_entry *e, int argc, char **argv)
{
  BADARGS(3, 4, " handle HOSTS ?host?");
  if (argc == 4)
    addhost_by_handle(u->handle, argv[3]);
  else
    addhost_by_handle(u->handle, "none"); /* drummer */
  return TCL_OK;
}

static int hosts_gotshare(struct userrec *u, struct user_entry *e,
			  char *buf, int idx)
{
  /* doh, try to be too clever and it bites your butt */
  return 0;
}

struct user_entry_type USERENTRY_HOSTS =
{
  0,
  hosts_gotshare,
  hosts_dupuser,
  hosts_null,
  hosts_null,
  hosts_write_userfile,
  hosts_kill,
  def_get,
  hosts_set,
  hosts_tcl_get,
  hosts_tcl_set,
  hosts_display,
  "HOSTS"
};

int list_append(struct list_type **h, struct list_type *i)
{
  for (; *h; h = &((*h)->next));
  *h = i;
  return 1;
}

int list_delete(struct list_type **h, struct list_type *i)
{
  for (; *h; h = &((*h)->next))
    if (*h == i) {
      *h = i->next;
      return 1;
    }
  return 0;
}

int list_contains(struct list_type *h, struct list_type *i)
{
  for (; h; h = h->next)
    if (h == i) {
      return 1;
    }
  return 0;
}

int add_entry_type(struct user_entry_type *type)
{
  struct userrec *u;

  list_insert(&entry_type_list, type);
  for (u = userlist; u; u = u->next) {
    struct user_entry *e = find_user_entry(type, u);

    if (e && e->name) {
      e->type = type;
      e->type->unpack(u, e);
      free(e->name);
      e->name = NULL;
    }
  }
  return 1;
}

int del_entry_type(struct user_entry_type *type)
{
  struct userrec *u;

  for (u = userlist; u; u = u->next) {
    struct user_entry *e = find_user_entry(type, u);

    if (e && !e->name) {
      e->type->pack(u, e);
      e->name = malloc(strlen(e->type->name) + 1);
      strcpy(e->name, e->type->name);
      e->type = NULL;
    }
  }
  return list_delete((struct list_type **) &entry_type_list,
		     (struct list_type *) type);
}

struct user_entry_type *find_entry_type(char *name)
{
  struct user_entry_type *p;

  for (p = entry_type_list; p; p = p->next) {
    if (!egg_strcasecmp(name, p->name))
      return p;
  }
  return NULL;
}

struct user_entry *find_user_entry(struct user_entry_type *et,
				   struct userrec *u)
{
  struct user_entry **e, *t;

  for (e = &(u->entries); *e; e = &((*e)->next)) {
    if (((*e)->type == et) ||
	((*e)->name && !egg_strcasecmp((*e)->name, et->name))) {
      t = *e;
      *e = t->next;
      t->next = u->entries;
      u->entries = t;
      return t;
    }
  }
  return NULL;
}

void *get_user(struct user_entry_type *et, struct userrec *u)
{
  struct user_entry *e;

  if (u && (e = find_user_entry(et, u)))
    return et->get(u, e);
  return 0;
}

int set_user(struct user_entry_type *et, struct userrec *u, void *d)
{
  struct user_entry *e;
  int r;

  if (!u || !et)
    return 0;

  if (!(e = find_user_entry(et, u))) {
    e = malloc(sizeof(struct user_entry));

    e->type = et;
    e->name = NULL;
    e->u.list = NULL;
    list_insert((&(u->entries)), e);
  }
  r = et->set(u, e, d);
  if (!e->u.list) {
    list_delete((struct list_type **) &(u->entries), (struct list_type *) e);
    free(e);
  }
  return r;
}
