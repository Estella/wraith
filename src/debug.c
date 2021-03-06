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
 * debug.c -- handles:
 *   signal handling
 *   Context handling
 *   debug funtions
 *
 */


#include "common.h"
#include "debug.h"
#include "chanprog.h"
#include "net.h"
#include "shell.h"
#include "color.h"
#include "binary.h"
#include "userrec.h"
#include "main.h"
#include "dccutil.h"
#include <setjmp.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/mman.h>

bool		sdebug = 0;             /* enable debug output? */
bool		segfaulted = 0;


#ifdef DEBUG_CONTEXT
/* Context storage for fatal crashes */
char    cx_file[16][16];
char    cx_note[16][16];
int     cx_line[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int     cx_ptr = 0;
#endif /* DEBUG_CONTEXT */

void setlimits()
{
#ifndef CYGWIN_HACKS
  struct rlimit plim, fdlim, corelim;
#ifndef DEBUG
/*  struct rsslim, stacklim;
  rsslim.rlim_cur = 30720;
  rsslim.rlim_max = 30720;
  setrlimit(RLIMIT_RSS, &rsslim);
  stacklim.rlim_cur = 30720;
  stacklim.rlim_max = 30720;
  setrlimit(RLIMIT_STACK, &stacklim);
*/
  /* do NOT dump a core. */
  plim.rlim_cur = 60;
  plim.rlim_max = 60;
  corelim.rlim_cur = 0;
  corelim.rlim_max = 0;
#else /* DEBUG */
  plim.rlim_cur = 500;
  plim.rlim_max = 500;
  corelim.rlim_cur = RLIM_INFINITY;
  corelim.rlim_max = RLIM_INFINITY;
#endif /* !DEBUG */
  setrlimit(RLIMIT_CORE, &corelim);
#ifndef __sun__
  setrlimit(RLIMIT_NPROC, &plim);
#endif
  fdlim.rlim_cur = 300;
  fdlim.rlim_max = 300;
  setrlimit(RLIMIT_NOFILE, &fdlim);
#endif /* !CYGWIN_HACKS */
}

void init_debug()
{
  for (int i = 0; i < 16; i ++)
    Context;

#ifdef DEBUG_CONTEXT
  egg_bzero(&cx_file, sizeof cx_file);
  egg_bzero(&cx_note, sizeof cx_note);
#endif /* DEBUG_CONTEXT */
}

void sdprintf (const char *format, ...)
{
  if (sdebug) {
    char s[2001] = "";
    va_list va;

    va_start(va, format);
    egg_vsnprintf(s, sizeof(s), format, va);
    va_end(va);
    
    remove_crlf(s);

    if (!backgrd)
      dprintf(DP_STDOUT, "[D:%d] %s%s%s\n", mypid, BOLD(-1), s, BOLD_END(-1));
    else
      printf("[D:%d] %s%s%s\n", mypid, BOLD(-1), s, BOLD_END(-1));
  }
}

#ifdef NOTUSED
char* hexize(const unsigned char* data, size_t len) {
  static char buffers[5][513] = { "", "", "", "", "" };
  static int n = 0;
  char *buf = buffers[n++];

  buf[0] = 0;

  for (size_t i = 0; i < len; ++i) {
    if (i == 0)
      sprintf(buf, "%.2X", (int) (data[i]));
    else
      sprintf(buf, "%s %.2X", buf, (int) (data[i]));
  }

  buf[len * 3] = 0;

  if (n == 5) n = 0;
  return buf;
}
#endif

void printstr(unsigned char *str, int len)
{
#ifdef no
        static char *outstr;
        int i, n, c, usehex;
        char *s, *outend;
	int max_strlen = 64;
        int xflag = 0;

        outstr = (char *) my_calloc(1, 2 * max_strlen);
        outend = outstr + max_strlen * 2 - 10;

        n = (((max_strlen) < (len)) ? (max_strlen) : (len));
        usehex = 0;
        if (xflag > 1)
                usehex = 1;
        else if (xflag) {
                for (i = 0; i < n; i++) {
                        c = str[i];
                        if (len < 0 && c == '\0')
                                break;
                        if (!isprint(c) && !egg_isspace(c)) {
                                usehex = 1;
                                break;
                        }
                }
        }

        s = outstr;
        *s++ = '\"';

        if (usehex) {
                for (i = 0; i < n; i++) {
                        c = str[i];
                        if (len < 0 && c == '\0')
                                break;
                        sprintf(s, "\\x%02x", c);
                        s += 4;
                        if (s > outend)
                                break;
                }
        }
        else {
                for (i = 0; i < n; i++) {
                        c = str[i];
                        if (len < 0 && c == '\0')
                                break;
                        switch (c) {
                        case '\"': case '\'': case '\\':
                                *s++ = '\\'; *s++ = c; break;
                        case '\f':
                                *s++ = '\\'; *s++ = 'f'; break;
                        case '\n':
                                *s++ = '\\'; *s++ = 'n'; break;
                        case '\r':
                                *s++ = '\\'; *s++ = 'r'; break;
                        case '\t':
                                *s++ = '\\'; *s++ = 't'; break;
                        case '\v':
                                *s++ = '\\'; *s++ = 'v'; break;
                        default:
                                if (egg_isprint(c))
                                        *s++ = c;
                                else if (i < n - 1 && egg_isdigit(str[i + 1])) {
                                        sprintf(s, "\\%03o", c);
                                        s += 4;
                                }
                                else {
                                        sprintf(s, "\\%o", c);
                                        s += strlen(s);
                                }
                                break;
                        }
                        if (s > outend)
                                break;
                }
        }

        *s++ = '\"';
        if (i < len || (len < 0 && (i == n || s > outend))) {
                *s++ = '.'; *s++ = '.'; *s++ = '.';
        }
        *s = '\0';

        printf("%s\n", outstr);
#endif
}

#ifdef DEBUG_CONTEXT

#define CX(ptr) cx_file[ptr] && cx_file[ptr][0] ? cx_file[ptr] : "", cx_line[ptr], cx_note[ptr] && cx_note[ptr][0] ? cx_note[ptr] : ""
static void write_debug()
{
  char tmpout[150] = "";

  simple_snprintf(tmpout, sizeof tmpout, "* Last 3 contexts: %s/%d [%s], %s/%d [%s], %s/%d [%s]",
                                  CX(cx_ptr - 2), CX(cx_ptr - 1), CX(cx_ptr));
  putlog(LOG_MISC, "*", "%s (Paste to bryan)", tmpout);
  printf("%s\n", tmpout);
}
#endif /* DEBUG_CONTEXT */

#ifndef DEBUG_CONTEXT
static void got_bus(int) __attribute__ ((noreturn));
#endif /* DEBUG_CONTEXT */

static void got_bus(int z)
{
  signal(SIGBUS, SIG_DFL);
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("BUS ERROR -- CRASHING!", 1);
#ifdef DEBUG
  raise(SIGBUS);
#else
  exit(1);
#endif /* DEBUG */
}

#ifndef CYGWIN_HACKS
#ifdef __i386__
#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

struct stackframe {
  struct stackframe *ebp;
  unsigned long addr;
};

/*
  CALL x
  PUSH EBP
  MOV EBP, ESP

  0x10: EBP
  0x14: EIP

 */

static int
canaccess(void *addr)
{
  addr = (void *) (((unsigned long) addr / PAGESIZE) * PAGESIZE);
  if (mprotect(addr, PAGESIZE, PROT_READ | PROT_WRITE | PROT_EXEC))
    if (errno != EACCES)
      return 0;
  return 1;
}

struct stackframe *sf = NULL;
int stackdepth = 0;

void
stackdump(int idx)
{
  __asm__("movl %EBP, %EAX");
  __asm__("movl %EAX, sf");
  if (idx == 0)
    putlog(LOG_MISC, "*", "STACK DUMP");
  else
    dprintf(idx, "STACK DUMP\n");

  while (canaccess(sf) && stackdepth < 20 && sf->ebp) {
    if (idx == 0)
      putlog(LOG_MISC, "*", " %02d: 0x%08lx/0x%08lx", stackdepth, (unsigned long) sf->ebp, sf->addr);
    else
      dprintf(idx, " %02d: 0x%08lx/0x%08lx\n", stackdepth, (unsigned long) sf->ebp, sf->addr);
    sf = sf->ebp;
    stackdepth++;
  }
  stackdepth = 0;
  sf = NULL;
  sleep(1);
}
#endif /* __i386__ */
#endif /* !CYGWIN_HACKS */

#ifndef DEBUG_CONTEXT
static void got_segv(int) __attribute__ ((noreturn));
#endif /* DEBUG_CONTEXT */

static void got_segv(int z)
{
  segfaulted = 1;
  alarm(0);		/* dont let anything jump out of this signal! */
  signal(SIGSEGV, SIG_DFL);
  /* stackdump(0); */
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("SEGMENT VIOLATION -- CRASHING!", 1);
#ifdef DEBUG
  raise(SIGSEGV);
#else
  exit(1);
#endif /* DEBUG */
}

static void got_fpe(int) __attribute__ ((noreturn));

static void got_fpe(int z)
{
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("FLOATING POINT ERROR -- CRASHING!", 0);
  exit(1);		/* for GCC noreturn */
}

static void got_term(int) __attribute__ ((noreturn));

static void got_term(int z)
{
  if (conf.bot->hub)
    write_userfile(-1);
  fatal("Received SIGTERM", 0);
  exit(1);		/* for GCC noreturn */
}

#ifndef DEBUG_CONTEXT
static void got_abort(int) __attribute__ ((noreturn));
#endif /* DEBUG_CONTEXT */

static void got_abort(int z)
{
  signal(SIGABRT, SIG_DFL);
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("GOT SIGABRT -- CRASHING!", 1);
#ifdef DEBUG
  raise(SIGSEGV);
#else
  exit(1);
#endif /* DEBUG */
}

#ifndef CYGWIN_HACKS
static void got_cont(int z)
{
  detected(DETECT_HIJACK, "POSSIBLE HIJACK DETECTED (!! MAY BE BOX REBOOT !!)");
}
#endif /* !CYGWIN_HACKS */

static void got_alarm(int) __attribute__((noreturn));

static void got_alarm(int z)
{
  sdprintf("SIGALARM");
  longjmp(alarmret, 1);

  /* -Never reached- */
}

/* Got ILL signal -- log context and continue
 */
static void got_ill(int z)
{
#ifdef DEBUG_CONTEXT
  putlog(LOG_MISC, "*", "* Context: %s/%d [%s]", cx_file[cx_ptr], cx_line[cx_ptr],
                         (cx_note[cx_ptr][0]) ? cx_note[cx_ptr] : "");
#endif /* DEBUG_CONTEXT */
}

static void
got_hup(int z)
{
  signal(SIGHUP, got_hup);
  putlog(LOG_MISC, "*", "GOT SIGHUP -- RESTARTING");
  do_restart = 1;
//  restart(-1);
}

static void
got_usr1(int z)
{
  signal(SIGUSR1, got_usr1);
  putlog(LOG_DEBUG, "*", "GOT SIGUSR1 -- RECHECKING BINARY");
  do_restart = 2;
//  reload_bin_data();
}

void init_signals() 
{
  signal(SIGBUS, got_bus);
  signal(SIGSEGV, got_segv);
  signal(SIGFPE, got_fpe);
  signal(SIGTERM, got_term);
#ifndef CYGWIN_HACKS
  signal(SIGCONT, got_cont);
#endif /* !CYGWIN_HACKS */
  signal(SIGABRT, got_abort);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGILL, got_ill);
  signal(SIGALRM, got_alarm);
  signal(SIGHUP, got_hup);
  signal(SIGUSR1, got_usr1);
}

#ifdef DEBUG_CONTEXT
/* Context */
void eggContext(const char *file, int line)
{
  char x[31] = "", *p = strrchr(file, '/');

  strlcpy(x, p ? p + 1 : file, sizeof x);
  cx_ptr = ((cx_ptr + 1) & 15);
  strcpy(cx_file[cx_ptr], x);
  cx_line[cx_ptr] = line;
  cx_note[cx_ptr][0] = 0;
}

/* Called from the ContextNote macro.
 */
void eggContextNote(const char *file, int line, const char *note)
{
  char x[31] = "", *p = strrchr(file, '/');

  strlcpy(x, p ? p + 1 : file, sizeof x);
  cx_ptr = ((cx_ptr + 1) & 15);
  strcpy(cx_file[cx_ptr], x);
  cx_line[cx_ptr] = line;
  strlcpy(cx_note[cx_ptr], note, sizeof cx_note[cx_ptr]);
}
#endif

