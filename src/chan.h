/*
 * chan.h
 *   stuff common to chan.c and mode.c
 *   users.h needs to be loaded too
 *
 * $Id$
 */

#ifndef _EGG_CHAN_H
#define _EGG_CHAN_H

typedef struct memstruct {
  char nick[NICKLEN];
  char userhost[UHOSTLEN];
  char server[SERVLEN];
  int hops;
  time_t joined;
  unsigned short flags;
  time_t split;			/* in case they were just netsplit	*/
  time_t last;			/* for measuring idle time		*/
  time_t delay;                  /* for delayed autoop                   */
  struct userrec *user;
  int tried_getuser;
  struct memstruct *next;
} memberlist;

#define CHANMETA "#&!+"
#define NICKVALID "[{}]^`|\\_-"

#define CHANOP       0x00001 /* channel +o                                   */
#define CHANVOICE    0x00002 /* channel +v                                   */
#define FAKEOP       0x00004 /* op'd by server                               */
#define SENTOP       0x00008 /* a mode +o was already sent out for this user */
#define SENTDEOP     0x00010 /* a mode -o was already sent out for this user */
#define SENTKICK     0x00020 /* a kick was already sent out for this user    */
#define SENTVOICE    0x00040 /* a mode +v was already sent out for this user */
#define SENTDEVOICE  0x00080 /* a devoice has been sent                      */
#define WASOP        0x00100 /* was an op before a split                     */
#define STOPWHO      0x00200
#define FULL_DELAY   0x00400
#define STOPCHECK    0x00800
#define EVOICE       0x01000 /* keeps people -v */
#define OPER         0x02000
/*                   0x02000 */
/*                   0x04000 */
/*                   0x08000 */
/*                   0x10000 */

#define chan_hasvoice(x) (x->flags & CHANVOICE)
#define chan_hasop(x) (x->flags & CHANOP)
#define chan_fakeop(x) (x->flags & FAKEOP)
#define chan_sentop(x) (x->flags & SENTOP)
#define chan_sentdeop(x) (x->flags & SENTDEOP)
#define chan_sentkick(x) (x->flags & SENTKICK)
#define chan_sentvoice(x) (x->flags & SENTVOICE)
#define chan_sentdevoice(x) (x->flags & SENTDEVOICE)
#define chan_issplit(x) (x->split > 0)
#define chan_wasop(x) (x->flags & WASOP)
#define chan_stopcheck(x) (x->flags & STOPCHECK)

/* Why duplicate this struct for exempts and invites only under another
 * name? <cybah>
 */
typedef struct maskstruct {
  char *mask;
  char *who;
  time_t timer;
  struct maskstruct *next;
} masklist;

/* Used for temporary bans, exempts and invites */
typedef struct maskrec {
  struct maskrec *next;
  char *mask,
       *desc,
       *user;
  time_t expire,
         added,
         lastactive;
  int flags;
} maskrec;
#ifdef S_IRCNET
extern maskrec *global_bans, *global_exempts, *global_invites;
#else
extern maskrec *global_bans;
#endif
#define MASKREC_STICKY 1
#define MASKREC_PERM   2

/* For every channel i join */
struct chan_t {
  memberlist *member;
  masklist *ban;
#ifdef S_IRCNET
  masklist *exempt;
  masklist *invite;
#endif
  char *topic;
  char *key;
  unsigned short int mode;
  int maxmembers;
  int members;
  int do_opreq;
  int jointime;
  int parttime;
#ifdef S_AUTOLOCK
  int fighting;
#endif
#ifdef G_BACKUP
  int backup_time;              /* If non-0, set +backup when now>backup_time */
#endif

};

#define CHANINV    0x0001	/* +i					*/
#define CHANPRIV   0x0002	/* +p					*/
#define CHANSEC    0x0004	/* +s					*/
#define CHANMODER  0x0008	/* +m					*/
#define CHANTOPIC  0x0010	/* +t					*/
#define CHANNOMSG  0x0020	/* +n					*/
#define CHANLIMIT  0x0040	/* -l -- used only for protecting modes	*/
#define CHANKEY    0x0080	/* +k					*/
#define CHANANON   0x0100	/* +a -- ircd 2.9			*/
#define CHANQUIET  0x0200	/* +q -- ircd 2.9			*/
#define CHANNOCLR  0x0400	/* +c -- bahamut			*/
#define CHANREGON  0x0800	/* +R -- bahamut			*/
#define CHANMODR   0x1000	/* +M -- bahamut			*/
#define CHANNOCTCP 0x2000      /* +C -- QuakeNet's ircu 2.10           */
#define CHANLONLY  0x4000      /* +r -- ircu 2.10.11                   */

#define MODES_PER_LINE_MAX 6

/* For every channel i'm supposed to be active on */
struct chanset_t {
  struct chanset_t *next;
  struct chan_t channel;	/* current information			*/
  char dname[81];               /* what the users know the channel as,
				   like !eggdev				*/
  char name[81];                /* what the servers know the channel
				   as, like !ABCDEeggdev		*/
  int flood_pub_thr;
  int flood_pub_time;
  int flood_join_thr;
  int flood_join_time;
  int flood_deop_thr;
  int flood_deop_time;
  int flood_kick_thr;
  int flood_kick_time;
  int flood_ctcp_thr;
  int flood_ctcp_time;
  int flood_nick_thr;
  int flood_nick_time;
  int status;
  int ircnet_status;
  int limitraise;
/* Chanint template 
 *int temp;
 */
  int idle_kick;
  int stopnethack_mode;
  int revenge_mode;
  int ban_time;
#ifdef S_IRCNET
  int invite_time;
  int exempt_time;

  maskrec *bans,		/* temporary channel bans		*/
          *exempts,		/* temporary channel exempts		*/
          *invites;		/* temporary channel invites		*/
#else
  maskrec *bans;
#endif
  /* desired channel modes: */
  int mode_pls_prot;		/* modes to enforce			*/
  int mode_mns_prot;		/* modes to reject			*/
  int limit_prot;		/* desired limit			*/
  char key_prot[121];		/* desired password			*/
/* Chanchar template
 *char temp[121];
 */
  /* queued mode changes: */
  char pls[21];			/* positive mode changes		*/
  char mns[21];			/* negative mode changes		*/
  char *key;			/* new key to set			*/
  char *rmkey;			/* old key to remove			*/
  int limit;			/* new limit to set			*/
  int bytes;			/* total bytes so far			*/
  int compat;			/* to prevent mixing old/new modes	*/
  struct {
    char * target;
  } opqueue[24];
  struct {
    char * target;
  } deopqueue[24];
  struct {
    char *op;
    int type;
  } cmode[MODES_PER_LINE_MAX];                 /* parameter-type mode changes -        */
  /* detect floods */
  char floodwho[FLOOD_CHAN_MAX][81];
  time_t floodtime[FLOOD_CHAN_MAX];
  int floodnum[FLOOD_CHAN_MAX];
  char deopd[NICKLEN];		/* last person deop'd (must change	*/
  int opreqtime[5];             /* remember when ops was requested */
#ifdef HUB
  char topic[91];
#endif

};

/* behavior modes for the channel */

/* Chanflag template 
 * #define CHAN_TEMP           0x0000
 */

#define CHAN_ENFORCEBANS    0x0001	   /* kick people who match channel bans */
#define CHAN_DYNAMICBANS    0x0002	   /* only activate bans when needed     */
#define CHAN_NOUSERBANS     0x0004	   /* don't let non-bots place bans      */
#define CHAN_CLOSED         0x0008	   /* Only users +o can join */
#define CHAN_BITCH          0x0010	   /* be a tightwad with ops             */
#define CHAN_TAKE 	    0x0020         /* When a bot gets opped, take the chan */
#define CHAN_PROTECTOPS     0x0040	   /* re-op any +o people who get deop'd */
#define CHAN_NOMOP	    0x0080         /* If a bot sees a mass op, botnet mdops */
#define CHAN_REVENGE        0x0100	   /* get revenge on bad people          */
#define CHAN_SECRET         0x0200	   /* don't advertise channel on botnet  */
#define CHAN_MANOP          0x0400         /* manual opping allowed? */
#define CHAN_CYCLE          0x0800	   /* cycle the channel if possible      */
#define CHAN_DONTKICKOPS    0x1000	   /* never kick +o flag people -arthur2 */
#define CHAN_INACTIVE       0x2000	   /* no irc support for this channel
                                         - drummer                           */
//#define CHAN_               0x4000	   /* unused */
#define CHAN_VOICE          0x8000	   /* a bot +y|y will voice *, except +q */
//#define CHAN_           0x10000
#define CHAN_REVENGEBOT     0x20000	   /* revenge on actions against the bot */
#define CHAN_NODESYNCH      0x40000
#define CHAN_FASTOP         0x80000        /* Bots will not use +o-b to op (no cookies) */ 
#define CHAN_PRIVATE         0x100000       /* users need |o to access chan */ 
#define CHAN_ACTIVE         0x1000000  /* like i'm actually on the channel
                                          and stuff                          */
#define CHAN_PEND           0x2000000  /* just joined; waiting for end of
                                          WHO list                           */
#define CHAN_FLAGGED        0x4000000  /* flagged during rehash for delete   */
//#define CHAN_ 		    0x8000000  /* unused      */
#define CHAN_ASKEDBANS      0x10000000
#define CHAN_ASKEDMODES     0x20000000 /* find out key-info on IRCu          */
#define CHAN_JUPED          0x40000000 /* Is channel juped                   */
#define CHAN_STOP_CYCLE     0x80000000 /* Some efnetservers have defined
                                          NO_CHANOPS_WHEN_SPLIT              */
#ifdef S_IRCNET
#define CHAN_ASKED_EXEMPTS  0x0001
#define CHAN_ASKED_INVITED  0x0002

#define CHAN_DYNAMICEXEMPTS 0x0004
#define CHAN_NOUSEREXEMPTS  0x0008
#define CHAN_DYNAMICINVITES 0x0010
#define CHAN_NOUSERINVITES  0x0020
#endif
/* prototypes */
memberlist *ismember(struct chanset_t *, char *);
struct chanset_t *findchan(const char *name);
struct chanset_t *findchan_by_dname(const char *name);

/* is this channel +s/+p? */
#define channel_hidden(chan) (chan->channel.mode & (CHANPRIV | CHANSEC))
/* is this channel +t? */
#define channel_optopic(chan) (chan->channel.mode & CHANTOPIC)

#define channel_active(chan)  (chan->status & CHAN_ACTIVE)
#define channel_pending(chan)  (chan->status & CHAN_PEND)
#define channel_bitch(chan) (chan->status & CHAN_BITCH)
#define channel_nodesynch(chan) (chan->status & CHAN_NODESYNCH)
#define channel_enforcebans(chan) (chan->status & CHAN_ENFORCEBANS)
#define channel_revenge(chan) (chan->status & CHAN_REVENGE)
#define channel_dynamicbans(chan) (chan->status & CHAN_DYNAMICBANS)
#define channel_nouserbans(chan) (chan->status & CHAN_NOUSERBANS)
#define channel_protectops(chan) (chan->status & CHAN_PROTECTOPS)
#define channel_autovoice(chan) (0)
#define channel_dontkickops(chan) (chan->status & CHAN_DONTKICKOPS)
#define channel_secret(chan) (chan->status & CHAN_SECRET)
#define channel_shared(chan) (1)
#define channel_cycle(chan) (chan->status & CHAN_CYCLE)
#define channel_inactive(chan) (chan->status & CHAN_INACTIVE)
#define channel_revengebot(chan) (chan->status & CHAN_REVENGEBOT)
#ifdef S_IRCNET
#define channel_dynamicexempts(chan) (chan->ircnet_status & CHAN_DYNAMICEXEMPTS)
#define channel_nouserexempts(chan) (chan->ircnet_status & CHAN_NOUSEREXEMPTS)
#define channel_dynamicinvites(chan) (chan->ircnet_status & CHAN_DYNAMICINVITES)
#define channel_nouserinvites(chan) (chan->ircnet_status & CHAN_NOUSERINVITES)
#endif
#define channel_juped(chan) (chan->status & CHAN_JUPED)
#define channel_stop_cycle(chan) (chan->status & CHAN_STOP_CYCLE)


#define channel_closed(chan) (chan->status & CHAN_CLOSED)
#define channel_take(chan) (chan->status & CHAN_TAKE)
#define channel_nomop(chan) (chan->status & CHAN_NOMOP)
#define channel_manop(chan) (chan->status & CHAN_MANOP)
#define channel_voice(chan) (chan->status & CHAN_VOICE)
#define channel_fastop(chan) (chan->status & CHAN_FASTOP)
#define channel_private(chan) (chan->status & CHAN_PRIVATE)
/* Chanflag template
 *#define channel_temp(chan) (chan->status & CHAN_PRIVATE)
 */



struct msgq_head {
  struct msgq *head;
  struct msgq *last;
  int tot;
  int warned;
};

/* Used to queue a lot of things */
struct msgq {
  struct msgq *next;
  int len;
  char *msg;
};

#endif				/* _EGG_CHAN_H */
