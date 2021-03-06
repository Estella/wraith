/* I hereby release this into the Public Domain - Bryan Drewery */
/*
 * shell.c -- handles:
 *
 * All shell related functions
 * -shell_exec()
 * -botconfig parsing
 * -check_*()
 * -crontab functions
 *
 */


#include "common.h"
#include "shell.h"
#include "chanprog.h"
#include "set.h"
#include "settings.h"
#include "userrec.h"
#include "net.h"
#include "flags.h"
#include "tandem.h"
#include "main.h"
#include "dccutil.h"
#include "misc.h"
#include "misc_file.h"
#include "bg.h"
#include "stat.h"
#include "users.h"
#include "botnet.h"
#include "src/mod/server.mod/server.h"

#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#ifdef HAVE_SYS_PTRACE_H
# include <sys/ptrace.h>
#endif /* HAVE_SYS_PTRACE_H */
#include <sys/wait.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

bool clear_tmpdir = 0;

int my_system(const char *run)
{
#ifdef WIN32NOFUCK
  int ret = -1;

  PROCESS_INFORMATION pinfo;
  STARTUPINFO sinfo;

  memset(&sinfo, 0, sizeof(STARTUPINFO));
  sinfo.cb = sizeof(sinfo);

  sinfo.wShowWindow = SW_HIDE;
  sinfo.dwFlags |= STARTF_USESTDHANDLES;
  sinfo.hStdInput =
  sinfo.hStdOutput =
  sinfo.hStdError =

  ret =
    CreateProcess(NULL, (char *) run, NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS | DETACHED_PROCESS, NULL, NULL,
                  (STARTUPINFO *) &sinfo, (PROCESS_INFORMATION *) &pinfo);

  if (ret == 0)
    return -1;
  else
    return 0;
#else
  return system(run);
#endif /* WIN32 */
}

void clear_tmp()
{
  if (!clear_tmpdir)
    return;

  DIR *tmp = NULL;

  if (!(tmp = opendir(tempdir))) 
    return;

  struct dirent *dir_ent = NULL;
  char *file = NULL;
  size_t flen = 0;

  while ((dir_ent = readdir(tmp))) {
    if (strncmp(dir_ent->d_name, ".pid.", 4) && 
        strncmp(dir_ent->d_name, ".u", 2) && 
        strcmp(dir_ent->d_name, ".bin.old") && 
        strncmp(dir_ent->d_name, STR(".socks-"), 7) &&
        strcmp(dir_ent->d_name, ".") && 
        strcmp(dir_ent->d_name, ".un") && 
        strcmp(dir_ent->d_name, "..")) {

      flen = strlen(dir_ent->d_name) + strlen(tempdir) + 1;
      file = (char *) my_calloc(1, flen);

      strlcat(file, tempdir, flen);
      strlcat(file, dir_ent->d_name, flen);
      file[strlen(file)] = 0;
      sdprintf("clear_tmp: %s", file);
      unlink(file);
      free(file);
    }
  }
  closedir(tmp);
  return;
}

void check_maxfiles()
{
  int sock = -1, sock1 = -1 , bogus = 0, failed_close = 0;

#ifdef USE_IPV6
  sock1 = getsock(0, AF_INET);		/* fill up any lower avail */
  sock = getsock(0, AF_INET);
#else
  sock1 = getsock(0);
  sock = getsock(0);
#endif

  if (sock1 != -1)
    killsock(sock1);
  
  if (sock == -1) {
    return;
  } else
    killsock(sock);

  bogus = sock - socks_total - 4;	//4 for stdin/stdout/stderr/dns 
 
  if (bogus >= 50) {			/* Attempt to close them */
    sdprintf("SOCK: %d BOGUS: %d SOCKS_TOTAL: %d", sock, bogus, socks_total);

    for (int i = 10; i < sock; i++)	/* dont close lower sockets, they're probably legit */
      if (!findanysnum(i)) {
        if ((close(i)) == -1)			/* try to close the BOGUS fd (likely a KQUEUE) */
          failed_close++;
        else
          bogus--;
      }
    if (bogus >= 150 || failed_close >= 50) {
      if (tands > 0) {
        botnet_send_chat(-1, conf.bot->nick, "Max FD reached, restarting...");
        botnet_send_bye("Max FD reached, restarting...");
      }

      nuke_server("brb");
      cycle_time = 0;
      restart(-1);
    } else if (bogus >= 100 && (bogus % 10) == 0) {
      putlog(LOG_WARN, "*", "* WARNING: $b%d$b bogus file descriptors detected, auto restart at 150", bogus);
    }
  }
}

void check_mypid()
{ 
  pid_t pid = 0;
  
  if (can_stat(conf.bot->pid_file))
    pid = checkpid(conf.bot->nick, NULL);

  if (pid && (pid != getpid()))
    fatal(STR("getpid() does not match pid in file. Possible cloned process, exiting.."), 0);
}


#ifndef CYGWIN_HACKS

char last_buf[128] = "";

void check_last() {
  if (login == DET_IGNORE)
    return;

  if (conf.username) {
    char *out = NULL, buf[50] = "";

    simple_snprintf(buf, sizeof(buf), STR("last -10 %s"), conf.username);
    if (shell_exec(buf, NULL, &out, NULL)) {
      if (out) {
        char *p = NULL;

        p = strchr(out, '\n');
        if (p)
          *p = 0;
        if (strlen(out) > 10) {
          if (last_buf[0]) {
            if (strncmp(last_buf, out, sizeof(last_buf))) {
              char *work = NULL;
              size_t siz = strlen(out) + 7 + 2 + 1;

              work = (char *) my_calloc(1, siz);

              simple_snprintf(work, siz, STR("Login: %s"), out);
              detected(DETECT_LOGIN, work);
              free(work);
            }
          }
          strlcpy(last_buf, out, sizeof(last_buf));
        }
        free(out);
      }
    }
  }
}

#ifdef NOT_USED
void check_processes()
{
  if (badprocess == DET_IGNORE)
    return;

  char *proclist = NULL, *out = NULL, *p = NULL, *np = NULL, *curp = NULL, buf[1024] = "", bin[128] = "";

  proclist = process_list[0] ? process_list : NULL;

  if (!proclist)
    return;

  if (!shell_exec("ps x", NULL, &out, NULL))
    return;

  /* Get this binary's filename */
  strlcpy(buf, shell_escape(binname), sizeof(buf));
  p = strrchr(buf, '/');
  if (p) {
    p++;
    strlcpy(bin, p, sizeof(bin));
  } else {
    bin[0] = 0;
  }
  /* Fix up the "permitted processes" list */
  p = (char *) my_calloc(1, strlen(proclist) + strlen(bin) + 6);
  strcpy(p, proclist);
  strcat(p, " ");
  strcat(p, bin);
  strcat(p, " ");
  proclist = p;
  curp = out;
  while (curp) {
    np = strchr(curp, '\n');
    if (np)
      *np++ = 0;
    if (atoi(curp) > 0) {
      char *pid = NULL, *tty = NULL, *mystat = NULL, *mytime = NULL, cmd[512] = "", line[2048] = "";

      strlcpy(line, curp, sizeof(line));
      /* it's a process line */
      /* Assuming format: pid tty stat time cmd */
      pid = newsplit(&curp);
      tty = newsplit(&curp);
      mystat = newsplit(&curp);
      mytime = newsplit(&curp);
      strlcpy(cmd, curp, sizeof(cmd));
      /* skip any <defunct> procs "/bin/sh -c" crontab stuff and binname crontab stuff */
      if (!strstr(cmd, "<defunct>") && !strncmp(cmd, "/bin/sh -c", 10) && 
          !strncmp(cmd, shell_escape(binname), strlen(shell_escape(binname)))) {
        /* get rid of any args */
        if ((p = strchr(cmd, ' ')))
          *p = 0;
        /* remove [] or () */
        if (strlen(cmd)) {
          p = cmd + strlen(cmd) - 1;
          if (((cmd[0] == '(') && (*p == ')')) || ((cmd[0] == '[') && (*p == ']'))) {
            *p = 0;
            strcpy(buf, cmd + 1);
            strcpy(cmd, buf);
          }
        }

        /* remove path */
        if ((p = strrchr(cmd, '/'))) {
          p++;
          strcpy(buf, p);
          strcpy(cmd, buf);
        }

        /* skip "ps" */
        if (strcmp(cmd, "ps")) {
          /* see if proc's in permitted list */
          strcat(cmd, " ");
          if ((p = strstr(proclist, cmd))) {
            /* Remove from permitted list */
            while (*p != ' ')
              *p++ = 1;
          } else {
            char *work = NULL;
            size_t size = 0;

            size = strlen(line) + 22;
            work = (char *) my_calloc(1, size);
            simple_snprintf(work, size, "Unexpected process: %s", line);
            detected(DETECT_PROCESS, work);
            free(work);
          }
        }
      }
    }
    curp = np;
  }
  free(proclist);
  if (out)
    free(out);
}
#endif /* NOT_USED */

void check_promisc()
{
#ifdef SIOCGIFCONF
  if (promisc == DET_IGNORE)
    return;

  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0)
    return;

  struct ifconf ifcnf;
  char buf[1024] = "";

  ifcnf.ifc_len = sizeof(buf);
  ifcnf.ifc_buf = buf;
  if (ioctl(sock, SIOCGIFCONF, &ifcnf) < 0) {
    close(sock);
    return;
  }

  char *reqp = NULL, *end_req = NULL;

  reqp = buf;				/* pointer to start of array */
  end_req = buf + ifcnf.ifc_len;	/* pointer to end of array */
  while (reqp < end_req) { 
    struct ifreq ifreq, *ifr = NULL;

    ifr = (struct ifreq *) reqp;	/* start examining interface */
    ifreq = *ifr;
    if (!ioctl(sock, SIOCGIFFLAGS, &ifreq)) {	/* we can read this interface! */
      /* sdprintf("Examing interface: %s", ifr->ifr_name); */
      if (ifreq.ifr_flags & IFF_PROMISC) {
        char which[101] = "";

        simple_snprintf(which, sizeof(which), STR("Detected promiscuous mode on interface: %s"), ifr->ifr_name);
        ioctl(sock, SIOCSIFFLAGS, &ifreq);	/* set flags */
        detected(DETECT_PROMISC, which);
	break;
      }
    }
    /* move pointer to next array element (next interface) */
    reqp += sizeof(ifr->ifr_name) + sizeof(ifr->ifr_addr);
  }
  close(sock);
#endif /* SIOCGIFCONF */
}

bool traced = 0;

static void got_sigtrap(int z)
{
  traced = 0;
}

void check_trace(int start)
{
  if (trace == DET_IGNORE || trace == DET_WARN)
    trace = DET_DIE;
//    return;

#ifdef DEBUG
  trace = DET_IGNORE;
#endif /* DEBUG */

  if (trace == DET_IGNORE)
    return;

  pid_t parent = getpid();

  /* we send ourselves a SIGTRAP, if we recieve, we're not being traced, otherwise we are. */
  signal(SIGTRAP, got_sigtrap);
  traced = 1;
  raise(SIGTRAP);
  /* no longer need this__asm__("INT3"); //SIGTRAP */
  signal(SIGTRAP, SIG_DFL);

  if (!traced) {
    signal(SIGINT, got_sigtrap);
    traced = 1;
    raise(SIGINT);
    signal(SIGINT, SIG_DFL);
  }

  if (traced) {
    if (start) {
      kill(parent, SIGKILL);
      exit(1);
    } else
      detected(DETECT_TRACE, STR("I'm being traced!"));
  } else {
    if (!start)
      return;

#ifndef __sun__
    int x, i;

  /* now, let's attempt to ptrace ourself */
    switch ((x = fork())) {
      case -1:
        return;
      case 0:		//child
        i = ptrace(PT_ATTACH, parent, 0, 0);
        /* EPERM is given on fbsd when security.bsd.unprivileged_proc_debug=0 */
        if (i == -1 && errno != EPERM) {
          if (start) {
            kill(parent, SIGKILL);
            exit(1);
          } else
            detected(DETECT_TRACE, STR("I'm being traced!"));
        } else {
          waitpid(parent, NULL, 0);
          ptrace(PT_DETACH, parent, (char *) 1, 0);
          kill(parent, SIGCHLD);
        }
        exit(0);
      default:		//parent
        waitpid(x, NULL, 0);
    }
#endif
  }
}
#endif /* !CYGWIN_HACKS */

int shell_exec(char *cmdline, char *input, char **output, char **erroutput)
{
  if (!cmdline)
    return 0;

  Tempfile *in = NULL, *out = NULL, *err = NULL;
  int x;
  int parent = getpid();

  /* Set up temp files */
  in = new Tempfile("in");
  if (!in || in->error) {
//    putlog(LOG_ERRORS, "*" , "exec: Couldn't open '%s': %s", in->file, strerror(errno)); 
    if (in)
      delete in;
    return 0;
  }

  if (input) {
    if (fwrite(input, strlen(input), 1, in->f) != 1) {
//      putlog(LOG_ERRORS, "*", "exec: Couldn't write to '%s': %s", in->file, strerror(errno));
      delete in;
      return 0;
    }
    fseek(in->f, 0, SEEK_SET);
  }

  err = new Tempfile("err");
  if (!err || err->error) {
//    putlog(LOG_ERRORS, "*", "exec: Couldn't open '%s': %s", err->file, strerror(errno));
    delete in;
    if (err)
      delete err;
    return 0;
  }

  out = new Tempfile("out");
  if (!out || out->error) {
//    putlog(LOG_ERRORS, "*", "exec: Couldn't open '%s': %s", out->file, strerror(errno));
    delete in;
    delete err;
    if (out)
      delete out;
    return 0;
  }

  x = fork();
  if (x == -1) {
    putlog(LOG_ERRORS, "*", "exec: fork() failed: %s", strerror(errno));
    delete in;
    delete err;
    delete out;
    return 0;
  }

  if (x) {		/* Parent: wait for the child to complete */
    int st = 0;
    size_t fs = 0;

    waitpid(x, &st, 0);
    /* child is now complete, read the files into buffers */
    delete in;
    fflush(out->f);
    fflush(err->f);
    if (erroutput) {
      char *buf = NULL;

      fseek(err->f, 0, SEEK_END);
      fs = ftell(err->f);
      if (fs == 0) {
        (*erroutput) = NULL;
      } else {
        buf = (char *) my_calloc(1, fs + 1);
        fseek(err->f, 0, SEEK_SET);
        fread(buf, 1, fs, err->f);
        buf[fs] = 0;
        (*erroutput) = buf;
      }
    }
    delete err;
    if (output) {
      char *buf = NULL;

      fseek(out->f, 0, SEEK_END);
      fs = ftell(out->f);
      if (fs == 0) {
        (*output) = NULL;
      } else {
        buf = (char *) my_calloc(1, fs + 1);
        fseek(out->f, 0, SEEK_SET);
        fread(buf, 1, fs, out->f);
        buf[fs] = 0;
        (*output) = buf;
      }
    }
    delete out;
    return 1;
  } else {
    /* Child: make fd's and set them up as std* */
//    int ind, outd, errd;
    char *argv[4] = { NULL, NULL, NULL, NULL };

//    ind = fileno(inpFile);
//    outd = fileno(outFile);
//    errd = fileno(errFile);
    if (dup2(in->fd, STDIN_FILENO) == (-1)) {
      kill(parent, SIGCHLD);
      exit(1);
    }
    if (dup2(out->fd, STDOUT_FILENO) == (-1)) {
      kill(parent, SIGCHLD);
      exit(1);
    }
    if (dup2(err->fd, STDERR_FILENO) == (-1)) {
      kill(parent, SIGCHLD);
      exit(1);
    }
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = cmdline;
    argv[3] = NULL;
    execvp(argv[0], &argv[0]);
    kill(parent, SIGCHLD);
    exit(1);
  }
}

void suicide(const char *msg)
{
  char tmp[512] = "";

  putlog(LOG_WARN, "*", STR("Comitting suicide: %s"), msg);
  simple_snprintf(tmp, sizeof(tmp), STR("Suicide: %s"), msg);
  set_user(&USERENTRY_COMMENT, conf.bot->u, tmp);
  if (!conf.bot->hub) {
    nuke_server(STR("kill the infidels!"));
    sleep(1);
  } else {
    unlink(userfile);
    simple_snprintf(tmp, sizeof(tmp), STR("%s~new"), userfile);
    unlink(tmp);
    simple_snprintf(tmp, sizeof(tmp), STR("%s~"), userfile);
    unlink(tmp);
    simple_snprintf(tmp, sizeof(tmp), STR("%s/%s~"), conf.datadir, userfile);
    unlink(tmp);
    simple_snprintf(tmp, sizeof(tmp), STR("%s/.u.0"), conf.datadir);
    unlink(tmp);
    simple_snprintf(tmp, sizeof(tmp), STR("%s/.u.1"), conf.datadir);
    unlink(tmp);
  }
  unlink(binname);

  if (conf.bot->localhub) {
    conf_checkpids(conf.bots);
    conf_killbot(conf.bots, NULL, NULL, SIGKILL);
  }
  unlink(conf.bot->pid_file);
  //Not recursively clearing these dirs as they may be ~USER/ ..
  unlink(conf.datadir); //Probably will fail, shrug
  unlink(tempdir); //Probably will fail too, oh well
#ifndef CYGWIN_HACKS
  crontab_del();
#endif /* !CYGWIN_HACKS */
  fatal(msg, 0);
}

void detected(int code, const char *msg)
{
  char tmp[512] = "";
  struct flag_record fr = { FR_GLOBAL, 0, 0, 0 };
  int act = DET_WARN, do_fatal = 0, killbots = 0;
  
  if (code == DETECT_LOGIN)
    act = login;
  if (code == DETECT_TRACE)
    act = trace;
  if (code == DETECT_PROMISC)
    act = promisc;
#ifdef NOT_USED
  if (code == DETECT_PROCESS)
    act = badprocess;
#endif
  if (code == DETECT_HIJACK)
    act = hijack;

  switch (act) {
  case DET_IGNORE:
    break;
  case DET_WARN:
    putlog(LOG_WARN, "*", msg);
    break;
  case DET_REJECT:
    do_fork();
    putlog(LOG_WARN, "*", STR("Setting myself +d: %s"), msg);
    simple_snprintf(tmp, sizeof(tmp), "+d: %s", msg);
    set_user(&USERENTRY_COMMENT, conf.bot->u, tmp);
    fr.global = USER_DEOP;
    fr.bot = 1;
    set_user_flagrec(conf.bot->u, &fr, 0);
    sleep(1);
    break;
  case DET_DIE:
    putlog(LOG_WARN, "*", STR("Dying: %s"), msg);
    simple_snprintf(tmp, sizeof(tmp), STR("Dying: %s"), msg);
    set_user(&USERENTRY_COMMENT, conf.bot->u, tmp);
    if (!conf.bot->hub)
      nuke_server(STR("BBL"));
    sleep(1);
    killbots++;
    do_fatal++;
    break;
  case DET_SUICIDE:
    suicide(msg);
    break;
  }
  if (killbots && conf.bot->localhub) {
    conf_checkpids(conf.bots);
    conf_killbot(conf.bots, NULL, NULL, SIGKILL);
  }
  if (do_fatal)
    fatal(msg, 0);
}

const char *werr_tostr(int errnum)
{
  switch (errnum) {
  case ERR_BINSTAT:
    return STR("Cannot access binary");
  case ERR_BADPASS:
    return STR("Incorrect password");
  case ERR_PASSWD:
    return STR("Cannot access the global passwd file");
  case ERR_WRONGBINDIR:
    return STR("Wrong directory/binary name");
  case ERR_DATADIR: 
    return STR("Cannot access datadir.");
  case ERR_TMPSTAT:
    return STR("Cannot access tmp directory.");
  case ERR_TMPMOD:
    return STR("Cannot chmod() tmp directory.");
  case ERR_WRONGUID:
    return STR("UID in binary does not match geteuid()");
  case ERR_WRONGUNAME:
    return STR("Uname in binary does not match uname()");
  case ERR_BADCONF:
    return STR("Config file is incomplete");
  case ERR_BADBOT:
    return STR("No such botnick");
  case ERR_BOTDISABLED:
    return STR("Bot is disabled, remove '/' in config");
  case ERR_NOBOTS:
    return STR("There are no bots in the binary! Please use ./binary -C to edit");
  case ERR_NOHOMEDIR:
    return STR("There is no homedir set. Please set one in the binary config with ./binary -C");
  case ERR_NOUSERNAME:
    return STR("There is no username set. Please set one in the binary config with ./binary -C");
  case ERR_NOBOT:
    return STR("I have no bot record but received -B???");
  case ERR_NOTINIT:
    return STR("Binary data is not initialized; try ./binary -C");
  default:
    return STR("Unforseen error");
  }

}

void werr(int errnum)
{
/* [1]+  Done                    ls --color=auto -A -CF
   [1]+  Killed                  bash
*/
/*
  int x = 0;
  unsigned long job = randint(2) + 1; */

/*  printf("[%lu] %lu%lu%lu%lu%lu\n", job, randint(2) + 1, randint(8) + 1, randint(8) + 1, errnum); */

/*    printf("\n[%lu]+  Killed                  rm -rf /\r\n", job); */
  /*
  if (checkedpass) {
    printf("[%lu] %d\n", job, getpid());
  }
  */
  /*  printf("\n[%lu]+  Stopped                 %s\r\n", job, basename(binname)); */
  sdprintf(STR("Error %d: %s"), errnum, werr_tostr(errnum));
  printf(STR("*** Error code %d\n\n"), errnum);
  printf(STR("Segmentation fault\n"));
  fatal("", 0);
  exit(0);				//gcc is stupid :)
}

int email(char *subject, char *msg, int who)
{
  struct utsname un;
  char run[2048] = "", addrs[100] = "";
  bool mail = 0, sendmail = 0;
  FILE *f = NULL;

  uname(&un);
  if (is_file(STR("/usr/sbin/sendmail")))
    sendmail = 1;
  else if (is_file(STR("/usr/bin/mail")))
    mail = 1;
  else {
    putlog(LOG_WARN, "*", STR("I Have no usable mail client."));
    return 1;
  }

  if (who & EMAIL_OWNERS)
    strlcpy(addrs, replace(settings.owneremail, ",", " "), sizeof(addrs));

  if (sendmail)
    strlcpy(run, STR("/usr/sbin/sendmail -t"), sizeof(run));
  else if (mail)
    simple_snprintf(run, sizeof(run), STR("/usr/bin/mail %s -a \"From: %s@%s\" -s \"%s\" -a \"Content-Type: text/plain\""), addrs, conf.bot->nick ? conf.bot->nick : "none", un.nodename, subject);

  if ((f = popen(run, "w"))) {
    if (sendmail) {
      fprintf(f, STR("To: %s\n"), addrs);
      fprintf(f, STR("From: %s@%s\n"), origbotname[0] ? origbotname : (conf.username ? conf.username : "WRAITH"), un.nodename);
      fprintf(f, STR("Subject: %s\n"), subject);
      fprintf(f, STR("Content-Type: text/plain\n"));
    }
    fprintf(f, "%s\n", msg);
    if (fflush(f))
      return 1;
    if (pclose(f))
      return 1;
  } else
    return 1;
  return 0;
}

void baduname(char *confhas, char *myuname) {
  char *tmpFile = NULL;
  int tosend = 0, make = 0;
  size_t siz = strlen(tempdir) + 3 + 1;

  tmpFile = (char *) my_calloc(1, siz);

  simple_snprintf(tmpFile, siz, STR("%s/.un"), conf.datadir);
  if (is_file(tmpFile)) {
    struct stat ss;
    time_t diff;

    stat(tmpFile, &ss);
    diff = now - ss.st_mtime;
    if (diff >= 86400) {
      tosend++;          		/* only send once a day */
      unlink(tmpFile);		/* remove file */
      make++;			/* make a new one at thie time. */
    }
  } else {
    make++;
  }

  if (make) {
    FILE *fp = NULL;
    if ((fp = fopen(tmpFile, "w"))) {
      fprintf(fp, "\n");
      fflush(fp);
      fclose(fp);
      tosend++;           /* only send if we could write the file. */
    }
  }

  if (tosend) {
    struct utsname un;
    char msg[1024] = "", subject[31] = "";

    uname(&un);
    simple_snprintf(subject, sizeof subject, STR("CONF/UNAME() mismatch notice"));
    simple_snprintf(msg, sizeof msg, STR("This is an auto email from a wraith bot which has you in it's OWNER_EMAIL list..\n \nThe uname() output on this box has changed, probably due to a kernel upgrade...\nMy login is: %s\nMy binary is: %s\nLocalhub: %s\nConf   : %s\nUname(): %s\n \nThis email will only be sent once a day while this error is present.\nYou need to login to my shell (%s) and fix my local config.\n"), 
                                  conf.username ? conf.username : "unknown", 
                                  binname,
                                  conf.bots && conf.bots->nick ? conf.bots->nick : origbotname,
                                  confhas, myuname, un.nodename);
    email(subject, msg, EMAIL_OWNERS);
  }
  free(tmpFile);
}

char *homedir(bool useconf)
{
  static char homedir_buf[DIRMAX] = "";

  if (!homedir_buf[0]) {
    char tmp[DIRMAX] = "";

    if (conf.homedir && useconf)
      simple_snprintf(tmp, sizeof tmp, "%s", conf.homedir);
    else {
#ifdef CYGWIN_HACKS
      simple_snprintf(tmp, sizeof tmp, "%s", dirname(binname));
#else /* !CYGWIN_HACKS */
      struct passwd *pw = NULL;
 
      ContextNote(STR("getpwuid()"));
      pw = getpwuid(myuid);
      if (pw)
        simple_snprintf(tmp, sizeof tmp, "%s", pw->pw_dir);
      ContextNote(STR("getpwuid(): Success"));
#endif /* CYGWIN_HACKS */
    }
    ContextNote(STR("realpath()"));
    if (tmp[0])
      realpath(tmp, homedir_buf); /* this will convert lame home dirs of /home/blah->/usr/home/blah */
    ContextNote(STR("realpath(): Success"));
  }
  return homedir_buf[0] ? homedir_buf : NULL;
}

char *my_username()
{
  static char username[DIRMAX] = "";

  if (!username[0]) {
#ifdef CYGWIN_HACKS
    simple_snprintf(username, sizeof username, "cygwin");
#else /* !CYGWIN_HACKS */
    struct passwd *pw = NULL;

    ContextNote(STR("getpwuid()"));
    pw = getpwuid(myuid);
    ContextNote(STR("getpwuid(): Success"));
    if (pw)
      strlcpy(username, pw->pw_name, sizeof(username));
#endif /* CYGWIN_HACKS */
  }
  return username[0] ? username : NULL;
}

int mkdir_p(const char *dir) {
  char *p = NULL, *path = NULL;

  path = p = strdup(dir);

  do {
    p = strchr(p + 1, '/');
    if (p)
      *p = '\0';
    if (can_stat(path) && !is_dir(path))
      unlink(path);
    if (!can_stat(path)) {
      if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR)) {
        unlink(path);
        mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
      }
    }
    if (p)
      *p = '/';
  } while(p);

  int couldStat = can_stat(path);

  free(path);

  return couldStat;
}

void expand_tilde(char **ptr)
{
  if (!conf.homedir || !conf.homedir[0])
    return;

  char *str = ptr ? *ptr : NULL;

  if (str && strchr(str, '~')) {
    char *p = NULL;
    size_t siz = strlen(str);

    if (str[siz - 1] == '/')
      str[siz - 1] = 0;

    if ((p = replace(str, "~", conf.homedir)))
      str_redup(ptr, p);
    else
      fatal("Unforseen error expanding '~'", 0);
  }
}

char *my_uname()
{
  static char os_uname[250] = "";

  if (!os_uname[0]) {
    char *unix_n = NULL, *vers_n = NULL;
    struct utsname un;

    if (uname(&un) < 0) {
      unix_n = "*unkown*";
      vers_n = "";
    } else {
      unix_n = un.nodename;
#ifdef __FreeBSD__
      vers_n = un.release;
#else /* __linux__ */
      vers_n = un.version;
#endif /* __FreeBSD__ */
    }
    simple_snprintf(os_uname, sizeof os_uname, "%s %s", unix_n, vers_n);
  }
  return os_uname;
}

#ifndef CYGWIN_HACKS
char *move_bin(const char *ipath, const char *file, bool run)
{
  char *path = strdup(ipath);

  expand_tilde(&path);

  /* move the binary to the correct place */
  static char newbin[DIRMAX] = "";
  char real[DIRMAX] = "";

  simple_snprintf(newbin, sizeof newbin, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", file);

  ContextNote(STR("realpath()"));
  realpath(binname, real);            /* get the realpath of binname */
  ContextNote(STR("realpath(): Success"));
  /* running from wrong dir, or wrong bin name.. lets try to fix that :) */
  sdprintf(STR("binname: %s"), binname);
  sdprintf(STR("newbin: %s"), newbin);
  sdprintf(STR("real: %s"), real);
  if (strcmp(binname, newbin) && strcmp(newbin, real)) {              /* if wrong path and new path != current */
    bool ok = 1;

    sdprintf(STR("wrong dir, is: %s :: %s"), binname, newbin);

    unlink(newbin);
    if (copyfile(binname, newbin))
      ok = 0;

    if (ok && !can_stat(newbin)) {
       unlink(newbin);
       ok = 0;
    }

    if (ok && fixmod(newbin)) {
        unlink(newbin);
        ok = 0;
    }

    if (ok) {
      sdprintf(STR("Binary successfully moved to: %s"), newbin);
      unlink(binname);
      if (run) {
        simple_snprintf(newbin, sizeof newbin, "%s%s%s", 
                        path, path[strlen(path) - 1] == '/' ? "" : "/", shell_escape(file));
        system(newbin);
        sdprintf(STR("exiting to let new binary run..."));
        exit(0);
      }
    } else {
      if (run)
        werr(ERR_WRONGBINDIR);
      sdprintf(STR("Binary move failed to: %s"), newbin);
      return binname;
    }
  }
  return newbin;
}

void check_crontab()
{
  int i = 0;

  if (!conf.bot->hub && !conf.bot->localhub)
    fatal(STR("something is wrong."), 0);
  if (!(i = crontab_exists())) {
    crontab_create(5);
    if (!(i = crontab_exists()))
      printf(STR("* Error writing crontab entry.\n"));
  }
}

void crontab_del() {
  char *tmpFile = NULL, *p = NULL, buf[2048] = "";

  size_t tmplen = strlen(binname) + 100;
  tmpFile = (char *) my_calloc(1, tmplen);

  strlcpy(tmpFile, shell_escape(binname), tmplen);
  if (!(p = strrchr(tmpFile, '/')))
    return;
  p++;
  strcpy(p, STR(".ctb"));
  simple_snprintf(buf, sizeof(buf), STR("crontab -l | grep -v '%s' | grep -v \"^#\" | grep -v \"^\\$\" > %s"), binname, tmpFile);
  if (shell_exec(buf, NULL, NULL, NULL)) {
    simple_snprintf(buf, sizeof(buf), STR("crontab %s"), tmpFile);
    shell_exec(buf, NULL, NULL, NULL);
  }
  unlink(tmpFile);
}

int crontab_exists() {
  char buf[2048] = "", *out = NULL;

  simple_snprintf(buf, sizeof buf, STR("crontab -l | grep '%s' | grep -v \"^#\""), binname);
  if (shell_exec(buf, NULL, &out, NULL)) {
    if (out && strstr(out, binname)) {
      free(out);
      return 1;
    } else {
      if (out)
        free(out);
      return 0;
    }
  } else
    return (-1);
}

void crontab_create(int interval) {
  char tmpFile[161] = "";
  FILE *f = NULL;
  int fd;

  simple_snprintf(tmpFile, sizeof tmpFile, STR("%s.crontab-XXXXXX"), tempdir);
  if ((fd = mkstemp(tmpFile)) == -1) {
    unlink(tmpFile);
    return;
  }

  char buf[256] = "";

  simple_snprintf(buf, sizeof buf, STR("crontab -l | grep -v \"%s\" | grep -v \"^#\" | grep -v \"^\\$\"> %s"), 
                  binname, tmpFile);
  if (shell_exec(buf, NULL, NULL, NULL) && (f = fdopen(fd, "a")) != NULL) {
    buf[0] = 0;
    if (interval == 1)
      strlcpy(buf, "*", 2);
    else {
      int i = 1;
      int si = randint(interval);

      while (i < 60) {
        if (buf[0])
          simple_snprintf(buf, sizeof(buf), "%s,%i", buf[0] ? buf : "", (i + si) % 60);
        else
          simple_snprintf(buf, sizeof(buf), "%i", (i + si) % 60);
        i += interval;
      }
    }
    simple_snprintf(buf + strlen(buf), sizeof buf, STR(" * * * * %s > /dev/null 2>&1"), binname);
    fseek(f, 0, SEEK_END);
    fprintf(f, "\n%s\n", buf);
    fclose(f);
    simple_snprintf(buf, sizeof(buf), STR("crontab %s"), tmpFile);
    shell_exec(buf, NULL, NULL, NULL);
  }
  close(fd);
  unlink(tmpFile);
}
#endif /* !CYGWIN_HACKS */

#ifdef CRAZY_TRACE
/* This code will attach a ptrace() to getpid() hence blocking process hijackers/tracers on the pid
 * only problem.. it just creates a new pid to be traced/hijacked :\
 */
int attached = 0;
void crazy_trace()
{
  pid_t parent = getpid();
  int x = fork();

  if (x == -1) {
    printf("Can't fork(): %s\n", strerror(errno));
  } else if (x == 0) {
    /* child */
    int i;
    i = ptrace(PTRACE_ATTACH, parent, (char *) 1, 0);
    if (i == -1) {
      printf("CANT PTRACE PARENT: errno: %d %s, i: %d\n", errno, strerror(errno), i);
      waitpid(parent, &i, 0);
      kill(parent, SIGCHLD);
      ptrace(PTRACE_DETACH, parent, 0, 0);
      kill(parent, SIGCHLD);
      exit(0);
    } else {
      printf("SUCCESSFUL ATTACH to %d: %d\n", parent, i);
      attached++;
    }
  } else {
    /* parent */
    printf("wait()\n");
    wait(&x);
  }
  printf("end\n");
}
#endif /* CRAZY_TRACE */

int det_translate(const char *word)
{
  if (word && word[0]) {
    if (!egg_strcasecmp(word, STR("ignore")))
      return DET_IGNORE;
    else if (!egg_strcasecmp(word, STR("warn")))
      return DET_WARN;
    else if (!egg_strcasecmp(word, STR("reject")))
      return DET_REJECT;
    else if (!egg_strcasecmp(word, STR("die")))
      return DET_DIE;
    else if (!egg_strcasecmp(word, STR("suicide")))
      return DET_SUICIDE;
  }
  return DET_IGNORE;
}

const char *det_translate_num(int num)
{
  switch (num) {
    case DET_IGNORE: return STR("ignore");
    case DET_WARN:   return STR("warn");
    case DET_REJECT: return STR("reject");
    case DET_DIE:    return STR("die");
    case DET_SUICIDE:return STR("suicide");
    default:         return STR("ignore");
  }
  return STR("ignore");
}

char *shell_escape(const char *path)
{
  static char ret1[DIRMAX] = "", ret2[DIRMAX] = "", *ret = NULL;
  static bool alt = 0;
  char *c = NULL;

  if (alt) {
    alt = 0;
    ret = ret1;
  } else {
    alt = 1;
    ret = ret2;
  }

  ret[0] = 0;

  for (c = (char *) path; c && *c; ++c) {
    if (strchr(ESCAPESHELL, *c))
      simple_snprintf(ret, sizeof(ret1), "%s\\%c", ret[0] ? ret : "", *c);
    else
      simple_snprintf(ret, sizeof(ret1), "%s%c", ret[0] ? ret : "", *c);
  }

  return ret;
}

