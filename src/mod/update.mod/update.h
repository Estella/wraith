/*
 * update.h -- part of update.mod
 *
 * $Id$
 */

#ifndef _EGG_MOD_update_update_H
#define _EGG_MOD_update_update_H

extern int bupdating, updated;

void finish_update(int);
void update_report(int, int);
void updatein(int, char *);

#endif				/* _EGG_MOD_update_update_H */
