/*
 * udefchan.c -- part of channels.mod
 *   user definable channel flags/settings
 *
 * $Id$
 */

static int getudef(struct udef_chans *ul, char *name)
{
  int val = 0;

  for (; ul; ul = ul->next)
    if (!egg_strcasecmp(ul->chan, name)) {
      val = ul->value;
      break;
    }
  return val;
}

int ngetudef(char *name, char *chan)
{
  struct udef_struct *l = NULL;
  struct udef_chans *ll = NULL;

  for (l = udef; l; l = l->next)
    if (!egg_strcasecmp(l->name, name)) {
      for (ll = l->values; ll; ll = ll->next)
        if (!egg_strcasecmp(ll->chan, chan))
          return ll->value;
      break;
    }
  return 0;
}

static void setudef(struct udef_struct *us, char *name, int value)
{
  struct udef_chans *ul = NULL, *ul_last = NULL;

  for (ul = us->values; ul; ul_last = ul, ul = ul->next)
    if (!egg_strcasecmp(ul->chan, name)) {
      ul->value = value;
      return;
    }

  ul = calloc(1, sizeof(struct udef_chans));
  ul->chan = strdup(name);
  ul->value = value;
  ul->next = NULL;
  if (ul_last)
    ul_last->next = ul;
  else
    us->values = ul;
}

void initudef(int type, char *name, int defined)
{
  struct udef_struct *ul = NULL, *ul_last = NULL;

  if (strlen(name) < 1)
    return;
  for (ul = udef; ul; ul_last = ul, ul = ul->next)
    if (ul->name && !egg_strcasecmp(ul->name, name)) {
      if (defined) {
        debug1("UDEF: %s defined", ul->name);
        ul->defined = 1;
      }
      return;
    }

  debug2("Creating %s (type %d)", name, type);
  ul = calloc(1, sizeof(struct udef_struct));
  ul->name = strdup(name);
  if (defined)
    ul->defined = 1;
  else
    ul->defined = 0;
  ul->type = type;
  ul->values = NULL;
  ul->next = NULL;
  if (ul_last)
    ul_last->next = ul;
  else
    udef = ul;
}

