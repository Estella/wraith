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
  struct memstruct *next;
  struct userrec *user;
  time_t joined;
  time_t split;			/* in case they were just netsplit	*/
  time_t last;			/* for measuring idle time		*/
  time_t delay;                  /* for delayed autoop                   */
  int hops;
  int tried_getuser;
  unsigned short flags;
  char nick[NICKLEN];
  char userhost[UHOSTLEN];
  char server[SERVLEN];
} memberlist;

#define CHANMETA "#&!+"
#define NICKVALID "[{}]^`|\\_-"

#define CHANOP       BIT0 /* channel +o                                   */
#define CHANVOICE    BIT1 /* channel +v                                   */
#define FAKEOP       BIT2 /* op'd by server                               */
#define SENTOP       BIT3 /* a mode +o was already sent out for this user */
#define SENTDEOP     BIT4 /* a mode -o was already sent out for this user */
#define SENTKICK     BIT5 /* a kick was already sent out for this user    */
#define SENTVOICE    BIT6 /* a mode +v was already sent out for this user */
#define SENTDEVOICE  BIT7 /* a devoice has been sent                      */
#define WASOP        BIT8 /* was an op before a split                     */
#define STOPWHO      BIT9
#define FULL_DELAY   BIT10
#define STOPCHECK    BIT11
#define EVOICE       BIT12 /* keeps people -v */
#define OPER         BIT13

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
  struct maskstruct *next;
  time_t timer;
  char *mask;
  char *who;
} masklist;

/* Used for temporary bans, exempts and invites */
typedef struct maskrec {
  struct maskrec *next;
  time_t expire,
         added,
         lastactive;
  int flags;
  char *mask,
       *desc,
       *user;
} maskrec;
extern maskrec *global_bans, *global_exempts, *global_invites;
#define MASKREC_STICKY 1
#define MASKREC_PERM   2

/* For every channel i join */
struct chan_t {
  memberlist *member;
  masklist *ban;
  masklist *exempt;
  masklist *invite;
  time_t jointime;
  time_t parttime;
  time_t no_op;
#ifdef S_AUTOLOCK
  int fighting;
#endif /* S_AUTOLOCK */
#ifdef G_BACKUP
  int backup_time;              /* If non-0, set +backup when now>backup_time */
#endif /* G_BACKUP */
  int maxmembers;
  int members;
  int do_opreq;
  char *topic;
  char *key;
  unsigned short int mode;
};

#define CHANINV    BIT0		/* +i					*/
#define CHANPRIV   BIT1		/* +p					*/
#define CHANSEC    BIT2		/* +s					*/
#define CHANMODER  BIT3		/* +m					*/
#define CHANTOPIC  BIT4		/* +t					*/
#define CHANNOMSG  BIT5		/* +n					*/
#define CHANLIMIT  BIT6		/* -l -- used only for protecting modes	*/
#define CHANKEY    BIT7		/* +k					*/
/* FIXME: JUST REMOVE THESE :D */
#define CHANANON   BIT8		/* +a -- ircd 2.9			*/
#define CHANQUIET  BIT9		/* +q -- ircd 2.9			*/
#define CHANNOCLR  BIT10	/* +c -- bahamut			*/
#define CHANREGON  BIT11	/* +R -- bahamut			*/
#define CHANMODR   BIT12	/* +M -- bahamut			*/
#define CHANNOCTCP BIT13        /* +C -- QuakeNet's ircu 2.10           */
#define CHANLONLY  BIT14        /* +r -- ircu 2.10.11                   */

#define MODES_PER_LINE_MAX 6

/* For every channel i'm supposed to be active on */
struct chanset_t {
  struct chanset_t *next;
  struct chan_t channel;	/* current information			*/
  maskrec *bans,		/* temporary channel bans		*/
          *exempts,		/* temporary channel exempts		*/
          *invites;		/* temporary channel invites		*/
  time_t added_ts;		/* ..and when? */
  struct {
    char *op;
    int type;
  } cmode[MODES_PER_LINE_MAX];                 /* parameter-type mode changes -        */
  /* detect floods */
  time_t floodtime[FLOOD_CHAN_MAX];
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
  int closed_ban;
  int closed_private;
/* Chanint template 
 *int temp;
 */
  int idle_kick;
  int stopnethack_mode;
  int revenge_mode;
  int ban_time;
  int invite_time;
  int exempt_time;

  /* desired channel modes: */
  int mode_pls_prot;		/* modes to enforce			*/
  int mode_mns_prot;		/* modes to reject			*/
  int limit_prot;		/* desired limit			*/
  /* queued mode changes: */
  int limit;			/* new limit to set			*/
  int bytes;			/* total bytes so far			*/
  int compat;			/* to prevent mixing old/new modes	*/
  int floodnum[FLOOD_CHAN_MAX];
  int opreqtime[5];             /* remember when ops was requested */

  char *key;			/* new key to set			*/
  char *rmkey;			/* old key to remove			*/
  char pls[21];			/* positive mode changes		*/
  char mns[21];			/* negative mode changes		*/
  char key_prot[121];		/* desired password			*/
/* Chanchar template
 *char temp[121];
 */
  char topic[121];
  char added_by[NICKLEN];	/* who added the channel? */
  char floodwho[FLOOD_CHAN_MAX][81];
  char deopd[NICKLEN];		/* last person deop'd (must change	*/
  char dname[81];               /* what the users know the channel as like !eggdev */
  char name[81];                /* what the servers know the channel as, like !ABCDEeggdev */
};

/* behavior modes for the channel */

/* Chanflag template 
 * #define CHAN_TEMP           0x0000
 */

#define CHAN_ENFORCEBANS    BIT0	/* kick people who match channel bans */
#define CHAN_DYNAMICBANS    BIT1	/* only activate bans when needed     */
#define CHAN_NOUSERBANS     BIT2	/* don't let non-bots place bans      */
#define CHAN_CLOSED         BIT3	/* Only users +o can join */
#define CHAN_BITCH          BIT4	/* be a tightwad with ops             */
#define CHAN_TAKE 	    BIT5	/* When a bot gets opped, take the chan */
#define CHAN_PROTECTOPS     BIT6	/* re-op any +o people who get deop'd */
#define CHAN_NOMOP	    BIT7        /* If a bot sees a mass op, botnet mdops */
#define CHAN_REVENGE        BIT8	/* get revenge on bad people          */
#define CHAN_SECRET         BIT9	/* don't advertise channel on botnet  */
#define CHAN_MANOP          BIT10       /* manual opping allowed? */
#define CHAN_CYCLE          BIT11	/* cycle the channel if possible      */
#define CHAN_INACTIVE       BIT12	/* no irc support for this channel */
#define CHAN_VOICE          BIT13	/* a bot +y|y will voice *, except +q */
#define CHAN_REVENGEBOT     BIT14	/* revenge on actions against the bot */
#define CHAN_NODESYNCH      BIT15
#define CHAN_FASTOP         BIT16	/* Bots will not use +o-b to op (no cookies) */ 
#define CHAN_PRIVATE        BIT17	/* users need |o to access chan */ 
#define CHAN_ACTIVE         BIT18	/* like i'm actually on the channel and stuff */
#define CHAN_PEND           BIT19	/* just joined; waiting for end of WHO list */
#define CHAN_FLAGGED        BIT20	/* flagged during rehash for delete   */
#define CHAN_ASKEDBANS      BIT21
#define CHAN_ASKEDMODES     BIT22	/* find out key-info on IRCu          */
#define CHAN_JUPED          BIT23	/* Is channel juped                   */
#define CHAN_STOP_CYCLE     BIT24	/* Some efnetservers have defined NO_CHANOPS_WHEN_SPLIT */

#define CHAN_ASKED_EXEMPTS  BIT0
#define CHAN_ASKED_INVITED  BIT1
#define CHAN_DYNAMICEXEMPTS BIT2
#define CHAN_NOUSEREXEMPTS  BIT3
#define CHAN_DYNAMICINVITES BIT4
#define CHAN_NOUSERINVITES  BIT5
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
#define channel_secret(chan) (chan->status & CHAN_SECRET)
#define channel_cycle(chan) (chan->status & CHAN_CYCLE)
#define channel_inactive(chan) (chan->status & CHAN_INACTIVE)
#define channel_revengebot(chan) (chan->status & CHAN_REVENGEBOT)
#define channel_dynamicexempts(chan) (chan->ircnet_status & CHAN_DYNAMICEXEMPTS)
#define channel_nouserexempts(chan) (chan->ircnet_status & CHAN_NOUSEREXEMPTS)
#define channel_dynamicinvites(chan) (chan->ircnet_status & CHAN_DYNAMICINVITES)
#define channel_nouserinvites(chan) (chan->ircnet_status & CHAN_NOUSERINVITES)
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
