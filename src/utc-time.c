/* $Id$
 * This file simply returns number of seconds since epoch in UTC 
 */

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

#include <time.h>
#include <stdio.h>

int main() {
  time_t now = time(NULL);

  printf("%li\n", mktime(gmtime(&now)));
  return 0;
}

