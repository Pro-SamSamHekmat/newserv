/* Copyright (C) Chris Porter 2005-2007 */
/* ALL RIGHTS RESERVED. */
/* Don't put this into the SVN repo. */

/*
  @todo
    - Write a nick printf type thing for pcalled functions.
    - Make commands register as apposed to blinding calling.
    - Use numerics instead of huge structures, and add lookup functions.
*/

#include "../channel/channel.h"
#include "../control/control.h"
#include "../nick/nick.h"
#include "../localuser/localuser.h"
#include "../localuser/localuserchannel.h"
#include "../lib/irc_string.h"
#include "../lib/flags.h"
#include "../authext/authext.h"
#include "../glines/glines.h"
#include "../trusts/trusts.h"
#include "../bans/bans.h"
#include "../core/modules.h"
#include "../geoip/geoip.h"

#include "lua.h"
#include "luabot.h"

#include <stdarg.h>
#include <stddef.h>

#define MAX_PUSHER 50

static int lua_smsg(lua_State *ps);
static int lua_skill(lua_State *ps);

typedef struct lua_pusher {
  short argtype;
  short offset;
  const char *structname;
} lua_pusher;

struct lua_pusher nickpusher[MAX_PUSHER];
struct lua_pusher chanpusher[MAX_PUSHER];
int nickpushercount, chanpushercount;

void lua_setuppusher(struct lua_pusher *pusherlist, lua_State *l, int index, struct lua_pusher **lp, int max, int pcount);
int lua_usepusher(lua_State *l, struct lua_pusher **lp, void *np);

void lua_initnickpusher(void);
void lua_initchanpusher(void);

#define lua_setupnickpusher(L2, I2, P2, M2) lua_setuppusher(&nickpusher[0], L2, I2, P2, M2, nickpushercount)
#define lua_setupchanpusher(L2, I2, P2, M2) lua_setuppusher(&chanpusher[0], L2, I2, P2, M2, chanpushercount)

int lua_cmsg(char *channell, char *message, ...) __attribute__ ((format (printf, 2, 3)));

int lua_cmsg(char *channell, char *message, ...) {
  char buf[512];
  va_list va;
  channel *cp;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  cp = findchannel(channell);
  if(!cp)
    return LUA_FAIL;

  if(!lua_lineok(buf))
    return LUA_FAIL;

  lua_channelmessage(cp, "%s", buf);

  return LUA_OK;
}

static int lua_chanmsg(lua_State *ps) {
  if(!lua_isstring(ps, 1))
    LUA_RETURN(ps, LUA_FAIL);

  LUA_RETURN(ps, lua_cmsg(LUA_PUKECHAN, "lua: %s", lua_tostring(ps, 1)));
}

static int lua_ctcp(lua_State *ps) {
  const char *n, *msg;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  n = lua_tostring(ps, 1);
  msg = lua_tostring(ps, 2);

  np = getnickbynick(n);
  if(!np || !lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  lua_message(np, "\001%s\001", msg);

  LUA_RETURN(ps, lua_cmsg(LUA_PUKECHAN, "lua-ctcp: %s (%s)", np->nick, msg));
}

static int lua_noticecmd(lua_State *ps) {
  const char *n, *msg;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  n = lua_tostring(ps, 1);
  msg = lua_tostring(ps, 2);

  np = getnickbynick(n);
  if(!np || !lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  lua_notice(np, "%s", msg);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_privmsgcmd(lua_State *ps) {
  const char *n, *msg;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  n = lua_tostring(ps, 1);
  msg = lua_tostring(ps, 2);

  np = getnickbynick(n);
  if(!np || !lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  lua_message(np, "%s", msg);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_kill(lua_State *ps) {
  const char *n, *msg;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  n = lua_tostring(ps, 1);
  msg = lua_tostring(ps, 2);

  np = getnickbynick(n);
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  if(IsOper(np) || IsService(np) || IsXOper(np))
    LUA_RETURN(ps, LUA_FAIL);

  if(!lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  killuser(lua_nick, np, "%s", msg);

  LUA_RETURN(ps, lua_cmsg(LUA_PUKECHAN, "lua-KILL: %s (%s)", np->nick, msg));
}

static int lua_kick(lua_State *ps) {
  const char *n, *msg, *chan;
  nick *np;
  channel *cp;
  int dochecks = 1;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2) || !lua_isstring(ps, 3))
    LUA_RETURN(ps, LUA_FAIL);

  chan = lua_tostring(ps, 1);
  n = lua_tostring(ps, 2);
  msg = lua_tostring(ps, 3);

  if(lua_isboolean(ps, 4) && !lua_toboolean(ps, 4))
    dochecks = 0;

  np = getnickbynick(n);
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  if(dochecks && (IsOper(np) || IsXOper(np) || IsService(np)))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)chan);
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  if(!lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  localkickuser(lua_nick, cp, np, msg);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_invite(lua_State *ps) {
  nick *np;
  channel *cp;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynick((char *)lua_tostring(ps, 1));
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 2));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  localinvite(lua_nick, cp->index, np);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_gline(lua_State *ps) {
  glineinfo *info;
  const char *reason;
  nick *target;
  int duration;
  
  if(!lua_isstring(ps, 1) || !lua_isint(ps, 2) || !lua_isstring(ps, 3))
    LUA_RETURN(ps, LUA_FAIL);

  duration = lua_toint(ps, 2);
  if((duration < 1) || (duration >  31 * 86400))
    LUA_RETURN(ps, LUA_FAIL);

  reason = lua_tostring(ps, 3);
  if(!lua_lineok(reason) || !reason)
    LUA_RETURN(ps, LUA_FAIL);

  target = getnickbynick(lua_tostring(ps, 1));
  if(!target || (IsOper(target) || IsXOper(target) || IsService(target)))
    LUA_RETURN(ps, LUA_FAIL);

  if(glinebynick(target, duration, reason, GLINE_SIMULATE, "lua") > 50)
    LUA_RETURN(ps, LUA_FAIL);

  info = glinebynickex(target, duration, reason, 0, "lua");
  LUA_RETURN(ps, lua_cmsg(LUA_PUKECHAN, "lua-GLINE: %s (%d users, %d seconds -- %s)", info->mask, info->hits, duration, reason));
}

static int lua_fastgetchaninfo(lua_State *ps) {
  static struct lua_pusher *ourpusher[MAX_PUSHER];
  channel *cp;

  if(!lua_isstring(ps, 1))
    return 0;

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp || cp->index->channel != cp)
    return 0;

  lua_setupchanpusher(ps, 2, ourpusher, MAX_PUSHER);
  return lua_usepusher(ps, ourpusher, cp->index);
}

static int lua_opchan(lua_State *ps) {
  channel *cp;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynick((char *)lua_tostring(ps, 2));
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodes(lua_nick, cp, np, MC_OP);
  LUA_RETURN(ps, LUA_OK);
}

static int lua_deopchan(lua_State *ps) {
  channel *cp;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynick((char *)lua_tostring(ps, 2));
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodes(lua_nick, cp, np, MC_DEOP);
  LUA_RETURN(ps, LUA_OK);
}

static int lua_voicechan(lua_State *ps) {
  channel *cp;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynick((char *)lua_tostring(ps, 2));
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodes(lua_nick, cp, np, MC_VOICE);
  LUA_RETURN(ps, LUA_OK);
}

static int lua_counthost(lua_State *ps) {
  long numeric;
  nick *np;

  if(!lua_islong(ps, 1))
    return 0;

  numeric = lua_tolong(ps, 1);

  np = getnickbynumeric(numeric);
  if(!np)
    return 0;

  lua_pushint(ps, np->host->clonecount);
  return 1;
}

static int lua_versioninfo(lua_State *ps) {
  lua_pushstring(ps, LUA_VERSION);
  lua_pushstring(ps, LUA_BOTVERSION);
  lua_pushstring(ps, __DATE__);
  lua_pushstring(ps, __TIME__);

  lua_pushstring(ps, LUA_AUXVERSION);

  return 5;
}

static int lua_nickmatchban(lua_State *ps) {
  const char *chanban_str;
  long numeric;
  chanban *cb;
  nick *np;

  if(!lua_islong(ps, 1) || !lua_isstring(ps, 2))
    return 0;

  numeric = lua_tolong(ps, 1);
  chanban_str = lua_tostring(ps, 2);

  np = getnickbynumeric(numeric);
  if(!np)
    return 0;

  cb = makeban(chanban_str);
  if(!cb)
    return 0;

  lua_pushboolean(ps, nickmatchban(np, cb, 1));

  freechanban(cb);

  return 1;
}

static int lua_nickbanned(lua_State *ps) {
  const char *channel_str;
  long numeric;
  channel *cp;
  nick *np;

  if(!lua_islong(ps, 1) || !lua_isstring(ps, 2))
    return 0;

  numeric = lua_tolong(ps, 1);
  channel_str = lua_tostring(ps, 2);

  np = getnickbynumeric(numeric);
  if(!np)
    return 0;

  cp = findchannel((char *)channel_str);
  if(!cp)
    return 0;

  lua_pushboolean(ps, nickbanned(np, cp, 1));

  return 1;
}

static int lua_suggestbanmask(lua_State *ps) {
  nick *np;
  glinebuf gb;
  gline *gl;
  long numeric;
  int i = 1;

  if (!lua_islong(ps, 1))
    return 0;

  numeric = lua_tolong(ps, 1);

  np = getnickbynumeric(numeric);
  if (!np)
    return 0;

  glinebufinit(&gb, 0);
  glinebufaddbynick(&gb, np, 0, "Auto", "None", time(NULL), time(NULL), time(NULL));

  lua_newtable(ps);

  for (gl = gb.glines; gl; gl = gl->next) {
    if (gl->host && gl->host->content) {
      char *mask = glinetostring(gl);
      LUA_APUSHSTRING(ps, i, mask);
      i++;
    }
  }

  glinebufabort(&gb);

  return 1;
}

static int lua_nickistrusted(lua_State *ps) {
  long numeric;
  nick *np;

  if(!lua_islong(ps, 1))
    return 0;

  numeric = lua_tolong(ps, 1);

  np = getnickbynumeric(numeric);
  if(!np)
    return 0;

  lua_pushboolean(ps, istrusted(np));

  return 1;
}

static int lua_basepath(lua_State *ps) {
  lua_pushfstring(ps, "%s/", cpath->content);

  return 1;
}

static int lua_botnick(lua_State *ps) {
  lua_pushstring(ps, luabotnick->content);

  return 1;
}

static int lua_numerictobase64(lua_State *ps) {
  if(!lua_islong(ps, 1))
    return 0;

  lua_pushstring(ps, longtonumeric(lua_tolong(ps, 1), 5));
  return 1;
}

static int lua_match(lua_State *ps) {
  const char *mask, *string;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    return 0;

  mask = lua_tostring(ps, 1);
  string = lua_tostring(ps, 2);

  if (!mask || !mask[0] || !string || !string[0])
    return 0;

  lua_pushboolean(ps, match2strings(mask, string));
  return 1;
}

static int lua_getuserbyauth(lua_State *l) {
  nick *np;
  int found = 0;
  authname *au;

  if(!lua_isstring(l, 1))
    return 0;

  au = getauthbyname(lua_tostring(l, 1));
  if(!au)
    return 0;

  for(np=au->nicks;np;np=np->nextbyauthname) {
    lua_pushnumeric(l, np->numeric);
    found++;
  }

  return found;
}

static int lua_getnickchans(lua_State *l) {
  nick *np;
  int i;
  channel **channels;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  channels = (channel **)np->channels->content;
  for(i=0;i<np->channels->cursi;i++)
    lua_pushstring(l, channels[i]->index->name->content);

  return np->channels->cursi;
}

static int lua_getnickchanindex(lua_State *l) {
  nick *np;
  int offset;

  if(!lua_islong(l, 1) || !lua_isint(l, 2))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  offset = lua_toint(l, 2);
  if((offset < 0) || (offset >= np->channels->cursi))
    return 0;

  lua_pushstring(l, ((channel **)np->channels->content)[offset]->index->name->content);

  return 1;
}

int hashindex;
nick *lasthashnick;

struct lua_pusher *nickhashpusher[MAX_PUSHER];

static int lua_getnextnick(lua_State *l) {
  if(!lasthashnick && (hashindex != -1))
    return 0;

  do {
    if(!lasthashnick) {
      hashindex++;
      if(hashindex >= NICKHASHSIZE)
        return 0;
      lasthashnick = nicktable[hashindex];
    } else {
      lasthashnick = lasthashnick->next;
    }
  } while(!lasthashnick);

  return lua_usepusher(l, nickhashpusher, lasthashnick);
}

static int lua_getfirstnick(lua_State *l) {
  hashindex = -1;
  lasthashnick = NULL;

  lua_setupnickpusher(l, 1, nickhashpusher, MAX_PUSHER);

  return lua_getnextnick(l);
}

int chanhashindex;
chanindex *lasthashchan;

struct lua_pusher *chanhashpusher[MAX_PUSHER];

static int lua_getnextchan(lua_State *l) {
  if(!lasthashchan && (chanhashindex != -1))
    return 0;

  do {
    if(!lasthashchan) {
      chanhashindex++;
      if(chanhashindex >= CHANNELHASHSIZE)
        return 0;
      lasthashchan = chantable[chanhashindex];
    } else {
      lasthashchan = lasthashchan->next;
    }
  } while(!lasthashchan || !lasthashchan->channel);

  return lua_usepusher(l, chanhashpusher, lasthashchan);
}

static int lua_getfirstchan(lua_State *l) {
  chanhashindex = -1;
  lasthashchan = NULL;

  lua_setupchanpusher(l, 1, chanhashpusher, MAX_PUSHER);

  return lua_getnextchan(l);
}

static int lua_getnickchancount(lua_State *l) {
  nick *np;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  lua_pushint(l, np->channels->cursi);

  return 1;
}

static int lua_gethostusers(lua_State *l) {
  nick *np;
  int count;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np || !np->host || !np->host->nicks)
    return 0;

  np = np->host->nicks;
  count = np->host->clonecount;

  do {
    lua_pushnumeric(l, np->numeric);
    np = np->nextbyhost;
  } while(np);

  return count;
}

static int lua_getnickcountry(lua_State *l) {
  nick *np;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  struct country *c = geoip_lookup_nick(np);
  if(!c)
    return 0;

  const char *code = geoip_code(c);
  if(!code)
    return 0;

  lua_pushstring(l, code);
  return 1;
}

static int lua_chanfix(lua_State *ps) {
  channel *cp;
  nick *np;

  if(!lua_isstring(ps, 1))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp || !cp->index)
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynick(LUA_CHANFIXBOT);
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  lua_message(np, "chanfix %s", cp->index->name->content);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_clearmode(lua_State *ps) {
  channel *cp;
  int i;
  nick *np;
  unsigned long *lp;
  modechanges changes;

  if(!lua_isstring(ps, 1))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp || !cp->users)
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodeinit(&changes, cp, lua_nick);

  localdosetmode_key(&changes, NULL, MCB_DEL);
  localdosetmode_simple(&changes, 0, CHANMODE_INVITEONLY | CHANMODE_LIMIT);

  while(cp->bans)
    localdosetmode_ban(&changes, bantostring(cp->bans), MCB_DEL);

  for(i=0,lp=cp->users->content;i<cp->users->hashsize;i++,lp++)
    if((*lp != nouser) && (*lp & CUMODE_OP)) {
      np = getnickbynumeric(*lp);
      if(np && !IsService(np))
        localdosetmode_nick(&changes, np, MC_DEOP);
    }

  localsetmodeflush(&changes, 1);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_ban(lua_State *ps) {
  channel *cp;
  const char *mask;
  modechanges changes;
  int dir = MCB_ADD;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  if(lua_isboolean(ps, 3) && lua_toboolean(ps, 3))
    dir = MCB_DEL;

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  mask = lua_tostring(ps, 2);
  if(!mask || !mask[0] || !lua_lineok(mask))
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodeinit(&changes, cp, lua_nick);
  localdosetmode_ban(&changes, mask, dir);
  localsetmodeflush(&changes, 1);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_topic(lua_State *ps) {
  channel *cp;
  char *topic;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  topic = (char *)lua_tostring(ps, 2);
  if(!topic || !lua_lineok(topic))
    LUA_RETURN(ps, LUA_FAIL);

  localsettopic(lua_nick, cp, topic);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_getuserchanmodes(lua_State *l) {
  nick *np;
  channel *cp;
  unsigned long *lp;

  if(!lua_islong(l, 1) || !lua_isstring(l, 2))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  cp = findchannel((char *)lua_tostring(l, 2));
  if(!cp || !cp->users)
    return 0;

  lp = getnumerichandlefromchanhash(cp->users, np->numeric);
  if(!lp)
    return 0;

  LUA_PUSHNICKCHANMODES(l, lp);
  return 1;
}

static int lua_getusermodes(lua_State *l) {
  nick *np;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  lua_pushstring(l, printflags(np->umodes, umodeflags));
  return 1;
}

static int lua_fastgetnickbynumeric(lua_State *l) {
  static struct lua_pusher *ourpusher[MAX_PUSHER];
  nick *np;

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  lua_setupnickpusher(l, 2, ourpusher, MAX_PUSHER);
  return lua_usepusher(l, ourpusher, np);
}

static int lua_fastgetnickbynick(lua_State *l) {
  static struct lua_pusher *ourpusher[MAX_PUSHER];
  nick *np;

  if(!lua_isstring(l, 1))
    return 0;

  np = getnickbynick((char *)lua_tostring(l, 1));
  if(!np)
    return 0;

  lua_setupnickpusher(l, 2, ourpusher, MAX_PUSHER);
  return lua_usepusher(l, ourpusher, np);
}

int channelnicklistindex, channelnicklistcount = -1;
channel *channelnicklist;

struct lua_pusher *channelnickpusher[MAX_PUSHER];

static int lua_getnextchannick(lua_State *l) {
  nick *np;

  do {
    channelnicklistindex++;

    if(channelnicklistindex >= channelnicklistcount)
      return 0;
  } while((channelnicklist->users->content[channelnicklistindex] == nouser) || !(np = getnickbynumeric(channelnicklist->users->content[channelnicklistindex])));

  return lua_usepusher(l, channelnickpusher, np);
}

static int lua_getfirstchannick(lua_State *l) {
  if(!lua_isstring(l, 1))
    return 0;

  channelnicklist = findchannel((char *)lua_tostring(l, 1));
  if(!channelnicklist|| !channelnicklist->users)
    return 0;

  channelnicklistindex = -1;
  channelnicklistcount = channelnicklist->users->hashsize;

  lua_setupnickpusher(l, 2, channelnickpusher, MAX_PUSHER);

  return lua_getnextchannick(l);
}

static int lua_nickonchan(lua_State *l) {
  int success = 0;
  if(lua_islong(l, 1) && lua_isstring(l, 2)) {
    channel *cp = findchannel((char *)lua_tostring(l, 2));
    if(cp && cp->users) {
      unsigned long *lp = getnumerichandlefromchanhash(cp->users, lua_tolong(l, 1));
      if(lp)
        success = 1;
    }    
  }

  lua_pushboolean(l, success);
  return 1;
}

static int lua_simplechanmode(lua_State *ps) {
  channel *cp;
  char *modes;
  flag_t add = 0, del = ~add;
  flag_t permitted = CHANMODE_NOEXTMSG | CHANMODE_TOPICLIMIT | CHANMODE_SECRET | CHANMODE_PRIVATE | CHANMODE_INVITEONLY | CHANMODE_MODERATE | CHANMODE_NOCOLOUR | CHANMODE_NOCTCP | CHANMODE_REGONLY | CHANMODE_DELJOINS | CHANMODE_NOQUITMSG | CHANMODE_NONOTICE | CHANMODE_MODNOAUTH | CHANMODE_SINGLETARG;
  modechanges changes;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  cp = findchannel((char *)lua_tostring(ps, 1));
  if(!cp)
    LUA_RETURN(ps, LUA_FAIL);

  modes = (char *)lua_tostring(ps, 2);
  if(!modes)
    LUA_RETURN(ps, LUA_FAIL);

  if(setflags(&add, permitted, modes, cmodeflags, REJECT_DISALLOWED|REJECT_UNKNOWN) != REJECT_NONE)
    LUA_RETURN(ps, LUA_FAIL);

  if(setflags(&del, permitted, modes, cmodeflags, REJECT_DISALLOWED|REJECT_UNKNOWN) != REJECT_NONE)
    LUA_RETURN(ps, LUA_FAIL);

  localsetmodeinit(&changes, cp, lua_nick);
  localdosetmode_simple(&changes, add, ~del);
  localsetmodeflush(&changes, 1);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_sethost(lua_State *ps) {
  char *ident, *host;
  nick *np;

  if(!lua_islong(ps, 1) || !lua_isstring(ps, 2) || !lua_isstring(ps, 3))
    LUA_RETURN(ps, LUA_FAIL);

  np = getnickbynumeric(lua_tolong(ps, 1));
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  ident = (char *)lua_tostring(ps, 2);
  host = (char *)lua_tostring(ps, 3);
  if(!lua_lineok(ident) || !lua_lineok(host))
    LUA_RETURN(ps, LUA_FAIL);

  sethostuser(np, ident, host);

  LUA_RETURN(ps, LUA_OK);
}

static int lua_getvisiblehostmask(lua_State *l) {
  nick *np;
  char buf[HOSTLEN+USERLEN+NICKLEN+REALLEN+10];

  if(!lua_islong(l, 1))
    return 0;

  np = getnickbynumeric(lua_tolong(l, 1));
  if(!np)
    return 0;

  lua_pushstring(l, visiblehostmask(np, buf));
  return 1;
}

void lua_registercommands(lua_State *l) {
  lua_register(l, "irc_smsg", lua_smsg);
  lua_register(l, "irc_skill", lua_skill);

  lua_register(l, "chanmsg", lua_chanmsg);
  lua_register(l, "versioninfo", lua_versioninfo);
  lua_register(l, "basepath", lua_basepath);
  lua_register(l, "botnick", lua_botnick);

  lua_register(l, "irc_report", lua_chanmsg);
  lua_register(l, "irc_ctcp", lua_ctcp);
  lua_register(l, "irc_kill", lua_kill);
  lua_register(l, "irc_kick", lua_kick);
  lua_register(l, "irc_invite", lua_invite);
  lua_register(l, "irc_gline", lua_gline);
  lua_register(l, "irc_counthost", lua_counthost);
  lua_register(l, "irc_getuserbyauth", lua_getuserbyauth);
  lua_register(l, "irc_notice", lua_noticecmd);
  lua_register(l, "irc_privmsg", lua_privmsgcmd);
  lua_register(l, "irc_opchan", lua_opchan);
  lua_register(l, "irc_voicechan", lua_voicechan);
  lua_register(l, "irc_chanfix", lua_chanfix);
  lua_register(l, "irc_clearmode", lua_clearmode);
  lua_register(l, "irc_ban", lua_ban);
  lua_register(l, "irc_deopchan", lua_deopchan);
  lua_register(l, "irc_topic", lua_topic);

  lua_register(l, "irc_getfirstnick", lua_getfirstnick);
  lua_register(l, "irc_getnextnick", lua_getnextnick);

  lua_register(l, "irc_getnickchans", lua_getnickchans);
  lua_register(l, "irc_getnickchanindex", lua_getnickchanindex);
  lua_register(l, "irc_getnickchancount", lua_getnickchancount);

  lua_register(l, "irc_getuserchanmodes", lua_getuserchanmodes);

  lua_register(l, "irc_getfirstchannick", lua_getfirstchannick);
  lua_register(l, "irc_getnextchannick", lua_getnextchannick);

  lua_register(l, "irc_gethostusers", lua_gethostusers);
  lua_register(l, "irc_getnickcountry", lua_getnickcountry);

  lua_register(l, "irc_getfirstchan", lua_getfirstchan);
  lua_register(l, "irc_getnextchan", lua_getnextchan);
  lua_register(l, "irc_getusermodes", lua_getusermodes);
  lua_register(l, "irc_nickonchan", lua_nickonchan);

  lua_register(l, "irc_fastgetnickbynumeric", lua_fastgetnickbynumeric);
  lua_register(l, "irc_fastgetnickbynick", lua_fastgetnickbynick);
  lua_register(l, "irc_fastgetchaninfo", lua_fastgetchaninfo);

  lua_register(l, "irc_getvisiblehostmask", lua_getvisiblehostmask);

  lua_register(l, "irc_simplechanmode", lua_simplechanmode);
  lua_register(l, "irc_sethost", lua_sethost);

  lua_register(l, "irc_numerictobase64", lua_numerictobase64);
  lua_register(l, "ircmatch", lua_match);

  lua_register(l, "irc_nickmatchban", lua_nickmatchban);
  lua_register(l, "irc_nickistrusted", lua_nickistrusted);
  lua_register(l, "irc_nickbanned", lua_nickbanned);
  lua_register(l, "irc_suggestbanmask", lua_suggestbanmask);
}

/* --- */

static int lua_smsg(lua_State *ps) {
  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  LUA_RETURN(ps, lua_cmsg((char *)lua_tostring(ps, 2), "%s", lua_tostring(ps, 1)));
}

static int lua_skill(lua_State *ps) {
  const char *n, *msg;
  nick *np;

  if(!lua_isstring(ps, 1) || !lua_isstring(ps, 2))
    LUA_RETURN(ps, LUA_FAIL);

  n = lua_tostring(ps, 1);
  msg = lua_tostring(ps, 2);

  np = getnickbynick(n);
  if(!np)
    LUA_RETURN(ps, LUA_FAIL);

  if(IsOper(np) || IsService(np) || IsXOper(np))
    LUA_RETURN(ps, LUA_FAIL);

  if(!lua_lineok(msg))
    LUA_RETURN(ps, LUA_FAIL);

  killuser(lua_nick, np, "%s", msg);

  LUA_RETURN(ps, LUA_OK);
}

#define PUSHER_STRING 1
#define PUSHER_REALNAME 2
#define PUSHER_IP 3
#define PUSHER_LONG 4
#define PUSHER_HOSTNAME 5
#define PUSHER_SSTRING 6
#define PUSHER_TOTALUSERS 7
#define PUSHER_TOPIC 8
#define PUSHER_UMODES 9
#define PUSHER_COUNTRY 10
#define PUSHER_REALUSERS 11
#define PUSHER_CHANMODES 12
#define PUSHER_TIMESTAMP 13
#define PUSHER_STRING_INDIRECT 14
#define PUSHER_ACC_ID 15
#define PUSHER_SERVER_NAME 16
#define PUSHER_SERVER_NUMERIC 17
#define PUSHER_IS_SERVICE 18

void lua_initnickpusher(void) {
  int i = 0;

#define PUSH_NICKPUSHER(F2, O2) nickpusher[i].argtype = F2; nickpusher[i].structname = #O2; nickpusher[i].offset = offsetof(nick, O2); i++;
#define PUSH_NICKPUSHER_CUSTOM(F2, custom) nickpusher[i].argtype = F2; nickpusher[i].structname = custom; nickpusher[i].offset = 0; i++;

  PUSH_NICKPUSHER(PUSHER_STRING, nick);
  PUSH_NICKPUSHER(PUSHER_STRING, ident);
  PUSH_NICKPUSHER(PUSHER_HOSTNAME, host);
  PUSH_NICKPUSHER(PUSHER_REALNAME, realname);
  PUSH_NICKPUSHER(PUSHER_STRING_INDIRECT, authname);
  PUSH_NICKPUSHER(PUSHER_IP, ipnode);
  PUSH_NICKPUSHER(PUSHER_LONG, numeric);
  PUSH_NICKPUSHER(PUSHER_LONG, timestamp);
  PUSH_NICKPUSHER(PUSHER_LONG, accountts);
  PUSH_NICKPUSHER(PUSHER_UMODES, umodes);
  PUSH_NICKPUSHER_CUSTOM(PUSHER_COUNTRY, "country");
  PUSH_NICKPUSHER_CUSTOM(PUSHER_ACC_ID, "accountid");
  PUSH_NICKPUSHER_CUSTOM(PUSHER_SERVER_NAME, "servername");
  PUSH_NICKPUSHER_CUSTOM(PUSHER_SERVER_NUMERIC, "servernumeric");
  PUSH_NICKPUSHER_CUSTOM(PUSHER_IS_SERVICE, "isservice");

  nickpushercount = i;
  nickpusher[i].argtype = 0;
}

void lua_setuppusher(struct lua_pusher *pusherlist, lua_State *l, int index, struct lua_pusher **lp, int max, int pcount) {
  int current = 0;

  if(max > 0)
    lp[0] = NULL;

  if(!lua_istable(l, index) || (max < 2))
    return;
    
  lua_pushnil(l);

  max--;

  while(lua_next(l, index)) {
    if(lua_isint(l, -1)) {
      int index = lua_toint(l, -1);
      if((index >= 0) && (index < pcount))
        lp[current++] = &pusherlist[index];
    }

    lua_pop(l, 1);

    if(current == max)
      break;
  }

  lp[current] = NULL;
}

int lua_usepusher(lua_State *l, struct lua_pusher **lp, void *np) {
  int i = 0;

  while(*lp) {
    void *offset = (void *)np + (*lp)->offset;

    switch((*lp)->argtype) {
      case PUSHER_STRING:
        lua_pushstring(l, (char *)offset);
        break;
      case PUSHER_STRING_INDIRECT:
        lua_pushstring(l, *(char **)offset);
        break;
      case PUSHER_HOSTNAME:
        lua_pushstring(l, (*(host **)offset)->name->content);
        break;
      case PUSHER_REALNAME:
        lua_pushstring(l, (*(realname **)offset)->name->content);
        break;
      case PUSHER_SSTRING:
        lua_pushstring(l, (*((sstring **)offset))->content);
        break;
      case PUSHER_LONG:
        lua_pushlong(l, *((long *)offset));
        break;
      case PUSHER_TIMESTAMP:
        lua_pushlong(l, (*((channel **)offset))->timestamp);
        break;
      case PUSHER_IP:
        lua_pushstring(l, IPtostr((*((patricia_node_t **)offset))->prefix->sin));
        break;
      case PUSHER_TOTALUSERS:
        lua_pushint(l, (*((channel **)offset))->users->totalusers);
        break;
      case PUSHER_CHANMODES:
        lua_pushstring(l, printallmodes(*((channel **)offset)));
        break;
      case PUSHER_ACC_ID:
        {
          nick *tnp = (nick *)np;
          if(IsAccount(tnp) && tnp->auth) {
            lua_pushlong(l, tnp->auth->userid);
          } else {
            lua_pushnil(l);
          }
          break;
        }
      case PUSHER_REALUSERS:
        {
          channel *cp = *((channel **)offset);
          nick *np2;
          int i, currentusers = countuniquehosts(cp);
          for(i=0;i<cp->users->hashsize;i++) {
            if(cp->users->content[i]==nouser)
              continue;

            if((np2=getnickbynumeric(cp->users->content[i]))==NULL) {
              Error("lua", ERR_ERROR, "Found unknown numeric %lu on channel %s", cp->users->content[i], cp->index->name->content);
              continue;
            }

            if (IsXOper(np2) || IsService(np2))
              currentusers--;
          }
          lua_pushint(l, currentusers);
        }
        break;
      case PUSHER_UMODES:
        lua_pushstring(l, printflags(*((flag_t *)offset), umodeflags));
        break;
      case PUSHER_TOPIC:
        if((*((channel **)offset))->topic) {
          lua_pushstring(l, (*((channel **)offset))->topic->content);
        } else {
          lua_pushnil(l);
        }
        break;
      case PUSHER_COUNTRY:
        {
          struct country *c = geoip_lookup_nick((nick *)offset);
          if(!c) {
            lua_pushint(l, -1);
          } else {
            const char *code = geoip_code(c);
            if(!code) {
              lua_pushint(l, -1);
            } else {
              lua_pushstring(l, code);
            }
          }
        }
        break;
      case PUSHER_SERVER_NAME:
        lua_pushstring(l, serverlist[homeserver(((nick *)offset)->numeric)].name->content);
        break;
      case PUSHER_SERVER_NUMERIC:
        lua_pushint(l, homeserver(((nick *)offset)->numeric));
        break;
      case PUSHER_IS_SERVICE:
        lua_pushboolean(l, NickOnServiceServer((nick *)offset));
        break;
    }

    i++;
    lp++;
  }

  return i;
}

void lua_initchanpusher(void) {
  int i = 0;

#define PUSH_CHANPUSHER(F2, O2, N2) chanpusher[i].argtype = F2; chanpusher[i].structname = N2; chanpusher[i].offset = offsetof(chanindex, O2); i++;

  PUSH_CHANPUSHER(PUSHER_SSTRING, name, "name");
  PUSH_CHANPUSHER(PUSHER_TOTALUSERS, channel, "totalusers");
  PUSH_CHANPUSHER(PUSHER_TOPIC, channel, "topic");
  PUSH_CHANPUSHER(PUSHER_REALUSERS, channel, "realusers");
  PUSH_CHANPUSHER(PUSHER_TIMESTAMP, channel, "timestamp");
  PUSH_CHANPUSHER(PUSHER_CHANMODES, channel, "modes");

  chanpushercount = i;
  chanpusher[i].argtype = 0;
}

