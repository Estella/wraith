/*
 * modvals.h
 *
 * $Id$
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002 Eggheads Development Team
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

#ifndef _EGG_MOD_MODVALS_H
#define _EGG_MOD_MODVALS_H

/* #define HOOK_GET_FLAGREC	  0 	*/
/* #define HOOK_BUILD_FLAGREC	  1 	*/
/* #define HOOK_SET_FLAGREC	  2 	*/
#define HOOK_READ_USERFILE	  3
#define HOOK_REHASH		  4
#define HOOK_MINUTELY		  5
#define HOOK_DAILY		  6
#define HOOK_HOURLY		  7
#define HOOK_USERFILE		  8
#define HOOK_SECONDLY		  9
#define HOOK_PRE_REHASH		 10
#define HOOK_IDLE		 11
#define HOOK_5MINUTELY		 12
#define HOOK_LOADED		 13
#define HOOK_BACKUP     	 14
#define HOOK_DIE		 15
#define HOOK_10SECONDLY          16
#define HOOK_30SECONDLY          17
#define HOOK_3SECONDLY           18
#define REAL_HOOKS		 19
//#define HOOK_CHANFILE            17
#define HOOK_SHAREOUT		105
#define HOOK_SHAREIN		106
#define HOOK_ENCRYPT_PASS	107
#define HOOK_QSERV		108
#define HOOK_ADD_MODE		109
#define HOOK_MATCH_NOTEREJ	110
#define HOOK_RFC_CASECMP	111
#define HOOK_DNS_HOSTBYIP	112
#define HOOK_DNS_IPBYHOST	113
#define HOOK_ENCRYPT_STRING     114
#define HOOK_DECRYPT_STRING     115
#define HOOK_SHAREUPDATEIN	117

/* These are FIXED once they are in a release they STAY
 */
#define MODCALL_START		  0
/* #define MODCALL_CLOSE		  1 */
#define MODCALL_EXPMEM		  2
#define MODCALL_REPORT		  3
/* Filesys */
#define FILESYS_REMOTE_REQ	  4
#define FILESYS_ADDFILE		  5
#define FILESYS_INCRGOTS	  6
#define FILESYS_ISVALID		  7
/* Share */
#define SHARE_FINISH		  4
#define UPDATE_FINISH		  4
#define SHARE_DUMP_RESYNC	  5
/* Channels */
#define CHANNEL_CLEAR		 15
#define CHANNEL_REMOVE		 45
/* Server */
#define SERVER_BOTNAME		  4
#define SERVER_BOTUSERHOST	  5
#define SERVER_NICKLEN		 38
#define SERVER_NUKESERVER	 19
/* IRC */
#define IRC_RECHECK_CHANNEL	  15
#define IRC_RECHECK_CHANNEL_MODES 17
#define IRC_DO_CHANNEL_PART	  19
#define IRC_CHECK_THIS_BAN	  20
#define IRC_CHECK_THIS_USER       21
/* Notes */
#define NOTES_CMD_NOTE		  4
/* Console */
#define CONSOLE_DOSTORE		  4
/* compress */
#define COMPRESS_CFILE		  5
#define COMPRESS_UFILE		  7

#ifdef HPUX_HACKS
#  include <dl.h>
#endif

typedef struct _module_entry {
  struct _module_entry	*next;
  char			*name;	/* Name of the module (without .so)	*/
  int			 major;	/* Major version number MUST match	*/
  int			 minor;	/* Minor version number MUST be >=	*/
  Function		*funcs;
#  ifdef DEBUG_MEM
  int			 mem_work;
#  endif	/* DEBUG_MEM */
} module_entry;

#endif		/* _EGG_MOD_MODVALS_H */
