/*
 * misc_file.h
 *   prototypes for misc_file.c
 *
 * $Id$
 */

#ifndef _EGG_MISC_FILE_H
#define _EGG_MISC_FILE_H

#ifndef MAKING_MODS
int copyfile(char *, char *);
int movefile(char *, char *);
int is_file(const char *);
int can_stat(const char *);
int can_lstat(const char *);
int is_symlink(const char *);
int is_dir(const char *);
int fixmod(const char *);
#endif /* !MAKING_MODS */

#endif				/* _EGG_MISC_FILE_H */
