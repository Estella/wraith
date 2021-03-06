#ifndef _MAIN_H
#define _MAIN_H

#include <sys/types.h>

enum {
  UPDATE_AUTO = 1,
  UPDATE_EXIT
};

enum {
  CONF_AUTO = 1,
  CONF_STATIC
};

extern int		role, default_flags, default_uflags, do_confedit,
			updating, do_restart;
extern bool		use_stderr, backgrd, used_B, term_z, loading, restart_was_update, restarting;
extern char		tempdir[], *binname, owner[], version[121], ver[], quit_msg[], *socksfile;
extern time_t		online_since, now, restart_time;
extern uid_t		myuid;
extern pid_t            mypid;
extern const time_t	buildts;
extern const char	*egg_version, *commit, *branch;

void fatal(const char *, int);

#endif /* !_MAIN_H */
