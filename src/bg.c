/*
 * bg.c -- handles:
 *   moving the process to the background, i.e. forking, while keeping threads
 *   happy.
 *
 * $Id$
 */

#include "common.h"
#include "bg.h"
#include "thread.h"
#include "main.h"
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

time_t lastfork = 0;
pid_t watcher;                  /* my child/watcher */

static void init_watcher(pid_t);

pid_t
do_fork()
{
  pid_t pid = 0;

  if (daemon(1, 1))
    fatal(strerror(errno), 0);

  pid = getpid();
  writepid(conf.bot->pid_file, pid);
  lastfork = now;
  if (conf.watcher)
    init_watcher(pid);
  return pid;
}

void
writepid(const char *pidfile, pid_t pid)
{
  FILE *fp = NULL;

  /* Need to attempt to write pid now, not later. */
  unlink(pidfile);
  if ((fp = fopen(pidfile, "w"))) {
    fprintf(fp, "%u\n", pid);
    if (fflush(fp)) {
      /* Kill bot incase a botchk is run from crond. */
      printf(EGG_NOWRITE, pidfile);
      printf("  Try freeing some disk space\n");
      fclose(fp);
      unlink(pidfile);
      exit(1);
    } else
      fclose(fp);
  } else
    printf(EGG_NOWRITE, pidfile);
}

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
