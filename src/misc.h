#ifndef _MISC_H
#define _MISC_H

#include "common.h"


/*
 * Set the following to the timestamp for the logfile entries.
 * Popular times might be "[%H:%M]" (hour, min), or "[%H:%M:%S]" (hour, min, sec)
 * Read `man strftime' for more formatting options.  Keep it below 32 chars.
 */
#define LOG_TS "[%H:%M]"


#define KICK_BANNED 		1
#define KICK_KUSER 		2
#define KICK_KICKBAN 		3
#define KICK_MASSDEOP 		4
#define KICK_BADOP 		5
#define KICK_BADOPPED 		6
#define KICK_MANUALOP 		7
#define KICK_MANUALOPPED	8
#define KICK_CLOSED		9
#define KICK_FLOOD 		10
#define KICK_NICKFLOOD 		11
#define KICK_KICKFLOOD 		12
#define KICK_BOGUSUSERNAME 	13
#define KICK_MEAN 		14
#define KICK_BOGUSKEY 		15


#ifndef MAKING_MODS
char *wbanner();
char *color(int, int, int);
void shuffle(char *, char *);
void showhelp(int, struct flag_record *, char *);
char *btoh(const unsigned char *, int);
void local_check_should_lock();
int listen_all(int, int);
char *replace(const char *, char *, char *);
int goodpass(char *, int, char *);
void makeplaincookie(char *, char *, char *);
int getting_users();
char *kickreason(int);
int bot_aggressive_to(struct userrec *);
int updatebin(int, char *, int);
int egg_strcatn(char *dst, const char *src, size_t max);
int my_strcpy(char *, char *);
void maskhost(const char *, char *);
char *stristr(char *, char *);
void splitc(char *, char *, char);
void splitcn(char *, char *, char, size_t);
void remove_crlf(char **);
char *newsplit(char **);
char *splitnick(char **);
void stridx(char *, char *, int);
void dumplots(int, const char *, char *);
void daysago(time_t, time_t, char *);
void days(time_t, time_t, char *);
void daysdur(time_t, time_t, char *);
void show_motd(int);
void show_channels(int, char *);
void show_banner(int);
char *extracthostname(char *);
void make_rand_str(char *, int);
int oatoi(const char *);
char *str_escape(const char *, const char, const char);
char *strchr_unescape(char *, const char, register const char);
void str_unescape(char *, register const char);
int str_isdigit(const char *);
void kill_bot(char *, char *);
#endif /* !MAKING_MODS */

#endif /* !_MISC_H_ */


