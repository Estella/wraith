/*
 * response.c -- handles:
 *
 *   What else?!
 *
 * $Id$
 */

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

#include "common.h"
#include "response.h"
#include "main.h"
#include "responses.h"

static response_t response_totals[RES_TYPES + 1];

void
init_responses()
{
  for (response_t i = 0; i <= RES_TYPES; i++)
    response_totals[i] = 0;
}

static void
count_responses(response_t type)
{
  response_t total = 0;

  for (total = 0; res[type][total]; total++) 
    ;

  response_totals[type] = total;
  /* printf("Type: %d has %d total responses!\n", type, response_totals[type]); */
}

const char *
response(response_t type)
{
  if (!type)
    type = randint(RES_TYPES) + 1;

  /* wait to count the totals until it's used for the first time */
  if (!response_totals[type])
    count_responses(type);

  return res[type][randint(response_totals[type])];
}

const char *
r_banned()
{
  if (!offensive_bans)
    return response(RES_BANNED);

  if (randint(1))
    return response(RES_BANNED);
  else
    return response(RES_BANNED_OFFENSIVE);
}
