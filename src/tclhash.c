/*
 * tclhash.c -- handles:
 *   bind and unbind
 *   checking and triggering the various in-bot bindings
 *   listing current bindings
 *   adding/removing new binding tables
 *   (non-Tcl) procedure lookups for msg/dcc/file commands
 *   (Tcl) binding internal procedures to msg/dcc/file commands
 *
 */

#include "main.h"
#include "chan.h"
#include "users.h"
#include "match.c"

extern Tcl_Interp	*interp;
extern struct dcc_t	*dcc;
extern struct userrec	*userlist;
extern int		 dcc_total;
extern time_t		 now;
extern mycmds		 cmdlist[];
extern int		 cmdi;

extern char		dcc_prefix[];

/* New bind table list */
static bind_table_t *bind_table_list_head = NULL;
static bind_table_t *BT_link;
static bind_table_t *BT_disc;
static bind_table_t *BT_away;
static bind_table_t *BT_dcc;
static bind_table_t *BT_bot;
static bind_table_t *BT_note;
static bind_table_t *BT_nkch;
static bind_table_t *BT_chat;
static bind_table_t *BT_act;
static bind_table_t *BT_bcst;
static bind_table_t *BT_chon;
static bind_table_t *BT_chof;
static bind_table_t *BT_chpt;
static bind_table_t *BT_chjn;


p_tcl_bind_list		bind_table_list;

static char *my_strdup(const char *s)
{
	char *t;

	t = (char *)nmalloc(strlen(s)+1);
	strcpy(t, s);
	return(t);
}


/* Allocate and initialise a chunk of memory.
 */
static inline void *n_malloc_null(int size, const char *file, int line)
{
#ifdef DEBUG_MEM
# define	nmalloc_null(size)	n_malloc_null(size, __FILE__, __LINE__)
  void	*ptr = n_malloc(size, file, line);
#else
# define	nmalloc_null(size)	n_malloc_null(size, NULL, 0)
  void	*ptr = nmalloc(size);
#endif

  egg_memset(ptr, 0, size);
  return ptr;
}

int expmem_tclhash(void)
{
  int			 tot = 0;

  return tot;
}


extern cmd_t C_dcc[];

void binds_init(void)
{
	bind_table_list_head = NULL;
	BT_link = add_bind_table2("link", 2, "ss", MATCH_MASK, BIND_STACKABLE);
        BT_nkch = add_bind_table2("nkch", 2, "ss", MATCH_MASK, BIND_STACKABLE);
	BT_disc = add_bind_table2("disc", 1, "s", MATCH_MASK, BIND_STACKABLE);
	BT_away = add_bind_table2("away", 3, "sis", MATCH_MASK, BIND_STACKABLE);
	BT_chon = add_bind_table2("chon", 2, "si", MATCH_MASK, BIND_USE_ATTR | BIND_STACKABLE);
	BT_chof = add_bind_table2("chof", 2, "si", MATCH_MASK, BIND_USE_ATTR | BIND_STACKABLE);
	BT_dcc = add_bind_table2("dcc", 3, "Uis", MATCH_MASK, BIND_USE_ATTR);
	BT_bot = add_bind_table2("bot", 3, "sss", MATCH_EXACT, 0);
        BT_note = add_bind_table2("note", 3 , "sss", MATCH_EXACT, 0);
	BT_chat = add_bind_table2("chat", 3, "sis", MATCH_MASK, BIND_STACKABLE | BIND_BREAKABLE);
	BT_act = add_bind_table2("act", 3, "sis", MATCH_MASK, BIND_STACKABLE);
	BT_bcst = add_bind_table2("bcst", 3, "sis", MATCH_MASK, BIND_STACKABLE);
        BT_chpt = add_bind_table2("chpt", 4, "ssii", MATCH_MASK, BIND_STACKABLE);
        BT_chjn = add_bind_table2("chjn", 6, "ssisis", MATCH_MASK, BIND_STACKABLE);

	add_builtins2(BT_dcc, C_dcc);
}

void init_old_binds(void)
{
  bind_table_list = NULL;
  Context;
}

void kill_bind2(void)
{
	while (bind_table_list_head) del_bind_table2(bind_table_list_head);
}

bind_table_t *add_bind_table2(const char *name, int nargs, const char *syntax, int match_type, int flags)
{
	bind_table_t *table;

	for (table = bind_table_list_head; table; table = table->next) {
		if (!strcmp(table->name, name)) return(table);
	}
	/* Nope, we have to create a new one. */
	table = (bind_table_t *)nmalloc(sizeof(*table));
	table->chains = NULL;
	table->name = my_strdup(name);
	table->nargs = nargs;
	table->syntax = my_strdup(syntax);
	table->match_type = match_type;
	table->flags = flags;
	table->next = bind_table_list_head;
	bind_table_list_head = table;
	return(table);
}

void del_bind_table2(bind_table_t *table)
{
	bind_table_t *cur, *prev;
	bind_chain_t *chain, *next_chain;
	bind_entry_t *entry, *next_entry;

	for (prev = NULL, cur = bind_table_list_head; cur; prev = cur, cur = cur->next) {
		if (!strcmp(table->name, cur->name)) break;
	}

	/* If it's found, remove it from the list. */
	if (cur) {
		if (prev) prev->next = cur->next;
		else bind_table_list_head = cur->next;
	}

	/* Now delete it. */
	nfree(table->name);
	for (chain = table->chains; chain; chain = next_chain) {
		next_chain = chain->next;
		for (entry = chain->entries; entry; entry = next_entry) {
			next_entry = entry->next;
			nfree(entry->function_name);
			nfree(entry);
		}
		nfree(chain);
	}
}

bind_table_t *find_bind_table2(const char *name)
{
	bind_table_t *table;

	for (table = bind_table_list_head; table; table = table->next) {
		if (!strcmp(table->name, name)) break;
	}
	return(table);
}

int del_bind_entry(bind_table_t *table, const char *flags, const char *mask, const char *function_name)
{
	bind_chain_t *chain;
	bind_entry_t *entry, *prev;

	/* Find the correct mask entry. */
	for (chain = table->chains; chain; chain = chain->next) {
		if (!strcmp(chain->mask, mask)) break;
	}
	if (!chain) return(1);

	/* Now find the function name in this mask entry. */
	for (prev = NULL, entry = chain->entries; entry; prev = entry, entry = entry->next) {
		if (!strcmp(entry->function_name, function_name)) break;
	}
	if (!entry) return(1);

	/* Delete it. */
	if (prev) prev->next = entry->next;
	else chain->entries = entry->next;
	nfree(entry->function_name);
	nfree(entry);

	return(0);
}

int add_bind_entry(bind_table_t *table, const char *flags, const char *mask, const char *function_name, int bind_flags, Function callback, void *client_data)
{
	bind_chain_t *chain;
	bind_entry_t *entry;

	/* Find the chain (mask) first. */
	for (chain = table->chains; chain; chain = chain->next) {
		if (!strcmp(chain->mask, mask)) break;
	}

	/* Create if it doesn't exist. */
	if (!chain) {
		chain = (bind_chain_t *)nmalloc(sizeof(*chain));
		chain->entries = NULL;
		chain->mask = my_strdup(mask);
		chain->next = table->chains;
		table->chains = chain;
	}

	/* If it's stackable */
	if (table->flags & BIND_STACKABLE) {
		/* Search for specific entry. */
		for (entry = chain->entries; entry; entry = entry->next) {
			if (!strcmp(entry->function_name, function_name)) break;
		}
	}
	else {
		/* Nope, just use first entry. */
		entry = chain->entries;
	}

	/* If we have an old entry, re-use it. */
	if (entry) nfree(entry->function_name);
	else {
		/* Otherwise, create a new entry. */
		entry = (bind_entry_t *)nmalloc(sizeof(*entry));
		entry->next = chain->entries;
		chain->entries = entry;
	}

	entry->function_name = my_strdup(function_name);
	entry->callback = callback;
	entry->client_data = client_data;
	entry->hits = 0;
	entry->bind_flags = bind_flags;

	entry->user_flags.match = FR_GLOBAL | FR_CHAN;
	break_down_flags(flags, &(entry->user_flags), NULL);

	return(0);
}

int findanyidx(register int z)
{
  register int j;

  for (j = 0; j < dcc_total; j++)
    if (dcc[j].sock == z)
      return j;
  return -1;
}

int check_bind(bind_table_t *table, const char *match, struct flag_record *_flags, ...)
{
	int *al; /* Argument list */
	struct flag_record *flags;
	bind_chain_t *chain;
	bind_entry_t *entry;
	int len, cmp, r, hits;
	Function cb;

	/* Experimental way to not use va_list... */
	flags = _flags;
	al = (int *)&_flags;

	/* Save the length for strncmp */
	len = strlen(match);

	/* Keep track of how many binds execute (or would) */
	hits = 0;

	/* Default return value is 0 */
	r = 0;

	/* For each chain in the table... */
	for (chain = table->chains; chain; chain = chain->next) {
		/* Test to see if it matches. */
		if (table->match_type & MATCH_PARTIAL) {
			if (table->match_type & MATCH_CASE) cmp = strncmp(match, chain->mask, len);
			else cmp = egg_strncasecmp(match, chain->mask, len);
		}
		else if (table->match_type & MATCH_MASK) {
			cmp = !wild_match_per((unsigned char *)chain->mask, (unsigned char *)match);
		}
		else {
			if (table->match_type & MATCH_CASE) cmp = strcmp(match, chain->mask);
			else cmp = egg_strcasecmp(match, chain->mask);
		}
		if (cmp) continue; /* Doesn't match. */

		/* Ok, now call the entries for this chain. */
		/* If it's not stackable, There Can Be Only One. */
		for (entry = chain->entries; entry; entry = entry->next) {
			/* Check flags. */
			if (table->match_type & BIND_USE_ATTR) {
				if (table->match_type & BIND_STRICT_ATTR) cmp = flagrec_eq(&entry->user_flags, flags);
				else cmp = flagrec_ok(&entry->user_flags, flags);
				if (!cmp) continue;
			}

			/* This is a hit */
			hits++;
		
			cb = entry->callback;
			if (entry->bind_flags & BIND_WANTS_CD) {
				al[0] = (int) entry->client_data;
				table->nargs++;
			}
			else al++;

			/* Default return value is 0. */
			r = 0;

			switch (table->nargs) {
				case 0: r = cb(); break;
				case 1: r = cb(al[0]); break;
				case 2: r = cb(al[0], al[1]); break;
				case 3: r = cb(al[0], al[1], al[2]); break;
				case 4: r = cb(al[0], al[1], al[2], al[3]); break;
				case 5: r = cb(al[0], al[1], al[2], al[3], al[4]); break;
				case 6: r = cb(al[0], al[1], al[2], al[3], al[4], al[5]); break;
				case 7: r = cb(al[0], al[1], al[2], al[3], al[4], al[5], al[6]);
				case 8: r = cb(al[0], al[1], al[2], al[3], al[4], al[5], al[6], al[7]);
			}

			if (entry->bind_flags & BIND_WANTS_CD) table->nargs--;
			else al--;

			if ((table->flags & BIND_BREAKABLE) && (r & BIND_RET_BREAK)) return(r);
		}
	}
	return(r);
}

/* Check for tcl-bound dcc command, return 1 if found
 * dcc: proc-name <handle> <sock> <args...>
 */
void check_tcl_dcc(const char *cmd, int idx, const char *args)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int x;
#ifdef S_DCCPASS
  int found = 0;
#endif

  get_user_flagrec(dcc[idx].user, &fr, dcc[idx].u.chat->con_chan);

#ifdef S_DCCPASS
  for (hm = H_dcc->first; hm; hm = hm->next) {
    if (!egg_strcasecmp(cmd, hm->mask)) {
      found = 1;
      break;
    }
  }
  if (found) {
    if (has_cmd_pass(cmd)) {
      char *p,
        work[1024],
        pass[128];
      p = strchr(args, ' ');
      if (p)
        *p = 0;
      strncpyz(pass, args, sizeof(pass));
      if (check_cmd_pass(cmd, pass)) {
        if (p)
          *p = ' ';
        strncpyz(work, args, sizeof(work));
        p = work;
        newsplit(&p);
        strcpy(args, p);
      } else {
        dprintf(idx, "Invalid command password. Use %scommand password arguments\n", dcc_prefix);
        putlog(LOG_MISC, "*", "%s attempted %s%s with missing or incorrect command password", dcc[idx].nick, dcc_prefix, cmd);
        return 0;
      }
    }
  }
#endif /* S_DCCPASS */
//    dprintf(idx, "What?  You need '%shelp'\n", dcc_prefix);
  x = check_bind(BT_dcc, cmd, &fr, dcc[idx].user, idx, args);
  if (x & BIND_RET_LOG) {
     putlog(LOG_CMDS, "*", "#%s# %s %s", dcc[idx].nick, cmd, args);
  }
}

void check_tcl_bot(const char *nick, const char *code, const char *param)
{
  check_bind(BT_bot, code, NULL, nick, code, param);
}

void check_tcl_chon(char *hand, int idx)
{
  struct flag_record     fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  struct userrec        *u;

  u = get_user_by_handle(userlist, hand);
  touch_laston(u, "partyline", now);
  get_user_flagrec(u, &fr, NULL);
  check_bind(BT_chon, hand, &fr, hand, idx);
}

void check_tcl_chof(char *hand, int idx)
{
  struct flag_record     fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  struct userrec        *u;

  u = get_user_by_handle(userlist, hand);
  touch_laston(u, "partyline", now);
  get_user_flagrec(u, &fr, NULL);
  check_bind(BT_chof, hand, &fr, hand, idx);
}

int check_tcl_chat(char *handle, int chan, const char *text)
{
  return check_bind(BT_chat, text, NULL, handle, chan, text);
}

void check_tcl_act(const char *from, int chan, const char *text)
{
  check_bind(BT_act, text, NULL, from, chan, text);
}

void check_tcl_bcst(const char *from, int chan, const char *text)
{
  check_bind(BT_bcst, text, NULL, from, chan, text);
}

void check_tcl_nkch(const char *ohand, const char *nhand)
{
  check_bind(BT_nkch, ohand, NULL, ohand, nhand);
}

void check_tcl_link(const char *bot, const char *via)
{
  check_bind(BT_link, bot, NULL, bot, via);
}

void check_tcl_disc(const char *bot)
{
  check_bind(BT_disc, bot, NULL, bot);
}

int check_tcl_note(const char *from, const char *to, const char *text)
{
  return check_bind(BT_note, to, NULL, from, to, text);
}

void check_tcl_chjn(const char *bot, const char *nick, int chan,
                    const char type, int sock, const char *host)
{
  struct flag_record    fr = {FR_GLOBAL, 0, 0, 0, 0, 0};
  char                  s[11], t[2];

  t[0] = type;
  t[1] = 0;
  switch (type) {
  case '^':
    fr.global = USER_ADMIN;
    break;
  case '*':
    fr.global = USER_OWNER;
    break;
  case '+':
    fr.global = USER_MASTER;
    break;
  case '@':
    fr.global = USER_OP;
    break;
  }
  egg_snprintf(s, sizeof s, "%d", chan);
  check_bind(BT_chjn, s, &fr, bot, nick, chan, t, sock, host);
}

void check_tcl_chpt(const char *bot, const char *hand, int sock, int chan)
{
  char  v[11];

  egg_snprintf(v, sizeof v, "%d", chan);
  check_bind(BT_chpt, v, NULL, bot, hand, sock, chan);
}

void check_tcl_away(const char *bot, int idx, const char *msg)
{
  check_bind(BT_away, bot, NULL, bot, idx, msg);
}

void add_builtins2(bind_table_t *table, cmd_t *cmds)
{
	char name[50];

	for (; cmds->name; cmds++) {
                /* add BT_dcc cmds to cmdlist[] :: add to the help system.. */
                if (!strcmp(table->name, "dcc") && !(cmds->nohelp)) {
		  cmdlist[cmdi].name = cmds->name;
                  cmdlist[cmdi].flags.match = FR_GLOBAL | FR_CHAN;
                  break_down_flags(cmds->flags, &(cmdlist[cmdi].flags), NULL);
                  cmdi++;
                }
		snprintf(name, 50, "*%s:%s", table->name, cmds->funcname ? cmds->funcname : cmds->name);
		add_bind_entry(table, cmds->flags, cmds->name, name, 0, cmds->func, NULL);
	}
}

void rem_builtins2(bind_table_t *table, cmd_t *cmds)
{
	char name[50];

	for (; cmds->name; cmds++) {
		sprintf(name, "*%s:%s", table->name, cmds->funcname ? cmds->funcname : cmds->name);
		del_bind_entry(table, cmds->flags, cmds->name, name);
	}
}
