/*
 * misc_file.h
 *   prototypes for misc_file.c
 *
 * $Id$
 */

#ifndef _EGG_MISC_FILE_H
#define _EGG_MISC_FILE_H

#include <stdio.h>

int copyfile(const char *, const char *);
int movefile(const char *, const char *);
int is_file(const char *);
int can_stat(const char *);
int can_lstat(const char *);
int is_symlink(const char *);
int is_dir(const char *);
int fixmod(const char *);

class Tempfile 
{
  public:
    Tempfile() { Tempfile(NULL); };					//constructor
    Tempfile(const char *prefix);		//constructor with file prefix

    void AllocTempfile();			//constructor with file prefix
    void Tempfile::my_close();
    ~Tempfile();				//destructor
    static bool FindDir();

    bool error;					//exceptions are lame.
    FILE *f;
    char *file;
    int fd;
    size_t len;

  private:
    char *prefix;
    int plen;
    void MakeTemp();				//Used for mktemp() and checking
};

#endif				/* _EGG_MISC_FILE_H */
