/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999 - 2002 Eggheads Development Team
 * Copyright (C) 2002 - 2008 Bryan Drewery
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * bg.c -- handles:
 *   moving the process to the background, i.e. forking, while keeping threads
 *   happy.
 *
 */


#include "common.h"
#include "bg.h"
#include "thread.h"
#include "main.h"
#include <signal.h>
#ifdef HAVE_SYS_PTRACE_H
# include <sys/ptrace.h>
#endif /* HAVE_SYS_PTRACE_H */
#ifndef CYGWIN_HACKS
#  include <sys/wait.h>
#endif /* !CYGWIN_HACKS */
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

time_t lastfork = 0;

#if !defined(CYGWIN_HACKS) && !defined(__sun__)
#ifdef NO
pid_t watcher;                  /* my child/watcher */

static void init_watcher(pid_t);
#endif
#endif /* !CYGWIN_HACKS */

int close_tty()
{
  int fd = -1;

  if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2)
      close(fd);
    return 1;
  }
  return 0;
}

static int my_daemon(int nochdir, int noclose)
{
  switch (fork()) {
    case -1:
      return (-1);
    case 0:
      break;
    default:
      _exit(0);
  }

  if (setsid() == -1)
    return (-1);

  if (!nochdir)
    (void) chdir("/");

  if (!noclose)
    close_tty();
  return (0);
}


pid_t
do_fork()
{
  if (my_daemon(1, 1))
    fatal(strerror(errno), 0);

  pid_t pid = getpid();

  writepid(conf.bot->pid_file, pid);
  lastfork = now;
#if !defined(CYGWIN_HACKS) && !defined(__sun__)
#ifdef NO
  if (conf.watcher)
    init_watcher(pid);
#endif
#endif /* !CYGWIN_HACKS */
  return pid;
}

void
writepid(const char *pidfile, pid_t pid)
{
  FILE *fp = NULL;

  sdprintf("Writing pid to: %s", pidfile);
  /* Need to attempt to write pid now, not later. */
  unlink(pidfile);
  if ((fp = fopen(pidfile, "w"))) {
    fprintf(fp, "%u\n", pid);
    if (fflush(fp)) {
      /* Kill bot incase a botchk is run from crond. */
      printf("* Warning!  Could not write %s file!\n", pidfile);
      printf("  Try freeing some disk space\n");
      fclose(fp);
      unlink(pidfile);
      exit(1);
    } else
      fclose(fp);
  } else
    printf("* Warning!  Could not write %s file!\n", pidfile);
}

#if !defined(CYGWIN_HACKS) && !defined(__sun__)
#ifdef NO
static void
init_watcher(pid_t parent)
{
  int x = fork();

  if (x == -1)
    fatal("Could not fork off a watcher process", 0);
  if (x != 0) {                 /* parent [bot] */
    watcher = x;
    /* printf("WATCHER: %d\n", watcher); */
    return;
  } else {                      /* child [watcher] */
    watcher = getpid();
    /* printf("MY PARENT: %d\n", parent); */
    /* printf("my pid: %d\n", watcher); */
    if (ptrace(PT_ATTACH, parent, 0, 0) == -1)
      fatal("Cannot attach to parent", 0);

    while (1) {
      int status = 0, sig = 0, ret = 0;

      waitpid(parent, &status, 0);
      sig = WSTOPSIG(status);
      if (sig) {
        ret = ptrace(PT_CONTINUE, parent, (char *) 1, sig);
        if (ret == -1)          /* send the signal! */
          fatal("Could not send signal to parent", 0);
        /* printf("Sent signal %s (%d) to parent\n", strsignal(sig), sig); */
      } else {
        ret = ptrace(PT_CONTINUE, parent, (char *) 1, 0);
        if (ret == -1) {
          if (errno == ESRCH)   /* parent is gone! */
            exit(0);            /* just exit */
          else
            fatal("Could not continue parent", 0);
        }
      }
    }
  }
}
#endif
#endif /* !CYGWIN_HACKS */
