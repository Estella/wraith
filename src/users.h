/*
 * users.h
 *   structures and definitions used by users.c and userrec.c
 *
 * $Id$
 */

#ifndef _EGG_USERS_H
#define _EGG_USERS_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* List functions :) , next *must* be the 1st item in the struct */
struct list_type {
  struct list_type *next;
  char *extra;
};

#define list_insert(a,b) {						\
    	(b)->next = *(a);						\
	*(a) = (b);							\
}
int list_append(struct list_type **, struct list_type *);
int list_delete(struct list_type **, struct list_type *);
int list_contains(struct list_type *, struct list_type *);


/* New userfile format stuff
 */
struct userrec;
struct user_entry;
struct user_entry_type {
  struct user_entry_type *next;
  int (*got_share) (struct userrec *, struct user_entry *, char *, int);
  int (*unpack) (struct userrec *, struct user_entry *);
#ifdef HUB
  int (*write_userfile) (FILE *, struct userrec *, struct user_entry *);
#endif /* HUB */
  int (*kill) (struct user_entry *);
  void *(*get) (struct userrec *, struct user_entry *);
  int (*set) (struct userrec *, struct user_entry *, void *);
  void (*display) (int idx, struct user_entry *, struct userrec *);
  char *name;
};


extern struct user_entry_type USERENTRY_COMMENT, USERENTRY_LASTON,
 USERENTRY_INFO, USERENTRY_BOTADDR, USERENTRY_HOSTS,
 USERENTRY_PASS, USERENTRY_BOTFL,
  USERENTRY_STATS,
  USERENTRY_ADDED,
  USERENTRY_MODIFIED,
  USERENTRY_CONFIG,
  USERENTRY_SECPASS;

struct laston_info {
  time_t laston;
  char *lastonplace;
};

struct bot_addr {
  port_t telnet_port;
  port_t relay_port;
  unsigned short hublevel;
  char *address;
  char *uplink;
  unsigned int roleid;
};

struct user_entry {
  struct user_entry *next;
  struct user_entry_type *type;
  union {
    char *string;
    void *extra;
    struct list_type *list;
    unsigned long ulong;
  } u;
  char *name;
};

struct xtra_key {
  struct xtra_key *next;
  char *key;
  char *data;
};

struct filesys_stats {
  int uploads;
  int upload_ks;
  int dnloads;
  int dnload_ks;
};

int add_entry_type(struct user_entry_type *);
struct user_entry_type *find_entry_type(char *);
struct user_entry *find_user_entry(struct user_entry_type *, struct userrec *);
void *get_user(struct user_entry_type *, struct userrec *);
int set_user(struct user_entry_type *, struct userrec *, void *);

#define bot_flags(u)	((long)get_user(&USERENTRY_BOTFL, (u)))
#define is_bot(u)	((u) && ((u)->flags & USER_BOT))
#define is_owner(u)	((u) && ((u)->flags & USER_OWNER))

/* Fake users used to store ignores and bans
 */
#define IGNORE_NAME "*ignore"
#define BAN_NAME    "*ban"
#define EXEMPT_NAME "*exempt"
#define INVITE_NAME "*Invite"
#define CHANS_NAME  "*channels"
#define CONFIG_NAME "*Config"

/* Channel-specific info
 */
struct chanuserrec {
  struct chanuserrec *next;
  char channel[81];
  time_t laston;
  flag_t flags;
  char *info;
};

/* New-style userlist
 */
struct userrec {
  struct userrec *next;
  char handle[HANDLEN + 1];
  flag_t flags;
  struct chanuserrec *chanrec;
  struct user_entry *entries;
};

struct igrec {
  struct igrec *next;
  char *igmask;
  time_t expire;
  char *user;
  time_t added;
  char *msg;
  int flags;
};
extern struct igrec *global_ign;

#define IGREC_PERM   2

/*
 * Note: Flags are in eggdrop.h
 */

struct userrec *adduser();
struct userrec *get_user_by_handle(struct userrec *, char *);
struct userrec *get_user_by_host(char *);
struct userrec *get_user_by_nick(char *);
struct userrec *check_chanlist();
struct userrec *check_chanlist_hand();

/* All the default userentry stuff, for code re-use
 */
int def_unpack(struct userrec *u, struct user_entry *e);
int def_kill(struct user_entry *e);
int def_write_userfile(FILE *f, struct userrec *u, struct user_entry *e);
void *def_get(struct userrec *u, struct user_entry *e);
int def_set(struct userrec *u, struct user_entry *e, void *buf);
int def_gotshare(struct userrec *u, struct user_entry *e,
		 char *data, int idx);
void def_display(int idx, struct user_entry *e, struct userrec *u);
int def_dupuser(struct userrec *new, struct userrec *old,
		struct user_entry *e);


#ifdef HUB
void backup_userfile();
#endif /* HUB */
void addignore(char *, char *, char *, time_t);
int delignore(char *);
void tell_ignores(int, char *);
int match_ignore(char *);
void check_expired_ignores();
void autolink_cycle(char *);
void tell_file_stats(int, char *);
void tell_user_ident(int, char *, int);
void tell_users_match(int, char *, int, int, int, char *);
int readuserfile(char *, struct userrec **);
void check_pmode();
void link_pref_val(struct userrec *u, char *lval);

extern char			natip[], userfile[];
extern int			ignore_time;

#endif				/* _EGG_USERS_H */
