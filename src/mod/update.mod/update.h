/*
 * update.h -- part of update.mod
 *
 * $Id$
 */

#ifndef _EGG_MOD_update_update_H
#define _EGG_MOD_update_update_H

/* Currently reserved flags for other modules:
 *      UFF_COMPRESS    0x000008	   Compress the user file
 *      UFF_ENCRYPT	0x000010	   Encrypt the user file
 */

/* Currently used priorities:
 *       90		UFF_ENCRYPT
 *      100             UFF_COMPRESS
 */

typedef struct {
  char	 *feature;		/* Name of the feature			*/
  int	  flag;			/* Flag representing the feature	*/
  int	(*ask_func)(int);	/* Pointer to the function that tells
				   us wether the feature should be
				   considered as on.			*/
  int	  priority;		/* Priority with which this entry gets
				   called.				*/
  int	(*snd)(int, char *);	/* Called before sending. Handled
				   according to `priority'.		*/
  int	(*rcv)(int, char *);	/* Called on receive. Handled according
				   to `priority'.			*/
} uff_table_t;

extern int bupdating;

void finish_update(int);
void update_report(int, int);

#endif				/* _EGG_MOD_update_update_H */
