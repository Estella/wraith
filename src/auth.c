/*
 * auth.c -- handles:
 *   auth system functions
 *   auth system hooks
 * $Id$
 */

#include "main.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "chan.h"
#include "tandem.h"
#include "modules.h"
#include <pwd.h>
#include <errno.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>


#include "stat.h"
#include "bg.h"

extern struct userrec *userlist;
extern struct dcc_t	*dcc;
extern struct chanset_t	*chanset;
extern time_t		 now;

#ifdef S_AUTH
extern char 		authkey[];
int auth_total = 0;
int max_auth = 100;
struct auth_t *auth = 0;
#endif /* S_AUTH */

/* Expected memory usage
 */
int expmem_auth()
{
  int tot = 0;

#ifdef S_AUTH
  tot += sizeof(struct auth_t) * max_auth;
#endif /* S_AUTH */

  return tot;
}

#ifdef S_AUTH
void init_auth_max()
{
  if (max_auth < 1)
    max_auth = 1;
  if (auth)
    auth = nrealloc(auth, sizeof(struct auth_t) * max_auth);
  else
    auth = nmalloc(sizeof(struct auth_t) * max_auth);
}

static void expire_auths()
{
  int i = 0, idle = 0;
  if (!ischanhub()) return;
  for (i = 0; i < auth_total;i++) {
    if (auth[i].authed) {
      idle = now - auth[i].atime;
      if (idle >= 60*60) {
        removeauth(i);
      }
    }
  }
}
#endif /* S_AUTH */

void init_auth()
{
#ifdef S_AUTH
  init_auth_max();
  add_hook(HOOK_MINUTELY, (Function) expire_auths);
#endif /* S_AUTH */
}

#ifdef S_AUTH
char *makehash(struct userrec *u, char *rand)
{
  int i = 0;
  MD5_CTX ctx;
  unsigned char md5out[33];
  char md5string[33], hash[500], *ret = NULL;
Context;
  sprintf(hash, "%s%s%s", rand, (char *) get_user(&USERENTRY_SECPASS, u), authkey ? authkey : "");

//  putlog(LOG_DEBUG, "*", STR("Making hash from %s %s: %s"), rand, get_user(&USERENTRY_SECPASS, u), hash);

  MD5_Init(&ctx);
  MD5_Update(&ctx, hash, strlen(hash));
  MD5_Final(md5out, &ctx);

  for(i=0; i<16; i++)
    sprintf(md5string + (i*2), "%.2x", md5out[i]);
   
//  putlog(LOG_DEBUG, "*", STR("MD5 of hash: %s"), md5string);
  ret = md5string;
  return ret;
}

int new_auth(void)
{
  int i = auth_total;

Context;
  if (auth_total == max_auth)
    return -1;

  auth_total++;
  egg_bzero((char *) &auth[i], sizeof(struct auth_t));
  return i;
}

int isauthed(char *host)
{
  int i = 0;
Context;
  if (!host || !host[0])
    return -1;
  for (i = 0; i < auth_total; i++) {
    if (auth[i].host[0] && !strcmp(auth[i].host, host)) {
      putlog(LOG_DEBUG, "*", STR("Debug for isauthed: checking: %s i: %d :: %s"), host, i, auth[i].host);
      return i;
    }
  }
  return -1;
}
  
void removeauth(int n)
{
Context;
  auth_total--;
  if (n < auth_total)
    egg_memcpy(&auth[n], &auth[auth_total], sizeof(struct auth_t));
  else
    egg_bzero(&auth[n], sizeof(struct auth_t)); /* drummer */
}
#endif /* S_AUTH */
