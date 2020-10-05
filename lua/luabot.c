/* Copyright (C) Chris Porter 2005 */
/* ALL RIGHTS RESERVED. */
/* Don't put this into the SVN repo. */

#include "../localuser/localuser.h"
#include "../localuser/localuserchannel.h"
#include "../core/schedule.h"
#include "../lib/irc_string.h"
#include "../core/config.h"

#include "lua.h"
#include "luabot.h"

#include <stdarg.h>

nick *lua_nick = NULL;
void *myureconnect = NULL, *myublip = NULL, *myutick = NULL;

void lua_bothandler(nick *target, int type, void **args);

void lua_onnewnick(int hooknum, void *arg);
void lua_blip(void *arg);
void lua_tick(void *arg);
void lua_onkick(int hooknum, void *arg);
void lua_ontopic(int hooknum, void *arg);
void lua_onauth(int hooknum, void *arg);
void lua_ondisconnect(int hooknum, void *arg);
void lua_onmode(int hooknum, void *arg);
void lua_onop(int hooknum, void *arg);
void lua_onvoice(int hooknum, void *arg);
void lua_onprequit(int hooknum, void *arg);
void lua_onquit(int hooknum, void *arg);
void lua_onrename(int hooknum, void *arg);
void lua_onconnect(int hooknum, void *arg);
void lua_onjoin(int hooknum, void *arg);
void lua_onpart(int hooknum, void *arg);
void lua_onbanevade(int hooknum, void *arg);

sstring *luabotnick;

void lua_registerevents(void) {
  registerhook(HOOK_CHANNEL_MODECHANGE, &lua_onmode);
  registerhook(HOOK_NICK_NEWNICK, &lua_onnewnick);
  registerhook(HOOK_IRC_DISCON, &lua_ondisconnect);
  registerhook(HOOK_IRC_PRE_DISCON, &lua_ondisconnect);
  registerhook(HOOK_NICK_ACCOUNT, &lua_onauth);
  registerhook(HOOK_CHANNEL_TOPIC, &lua_ontopic);
  registerhook(HOOK_CHANNEL_KICK, &lua_onkick);
  registerhook(HOOK_CHANNEL_OPPED, &lua_onop);
  registerhook(HOOK_CHANNEL_DEOPPED, &lua_onop);
  registerhook(HOOK_CHANNEL_VOICED, &lua_onvoice);
  registerhook(HOOK_CHANNEL_DEVOICED, &lua_onvoice);
  registerhook(HOOK_NICK_PRE_LOSTNICK, &lua_onprequit);
  registerhook(HOOK_NICK_LOSTNICK, &lua_onquit);
  registerhook(HOOK_NICK_RENAME, &lua_onrename);
  registerhook(HOOK_IRC_CONNECTED, &lua_onconnect);
  registerhook(HOOK_SERVER_END_OF_BURST, &lua_onconnect);
  registerhook(HOOK_CHANNEL_JOIN, &lua_onjoin);
  registerhook(HOOK_CHANNEL_PART, &lua_onpart);
  registerhook(HOOK_CHANNEL_CREATE, &lua_onjoin);
  registerhook(HOOK_CHANNEL_JOIN_BYPASS_BAN, &lua_onbanevade);
}

void lua_deregisterevents(void) {
  deregisterhook(HOOK_CHANNEL_PART, &lua_onpart);
  deregisterhook(HOOK_CHANNEL_CREATE, &lua_onjoin);
  deregisterhook(HOOK_CHANNEL_JOIN, &lua_onjoin);
  deregisterhook(HOOK_SERVER_END_OF_BURST, &lua_onconnect);
  deregisterhook(HOOK_IRC_CONNECTED, &lua_onconnect);
  deregisterhook(HOOK_NICK_RENAME, &lua_onrename);
  deregisterhook(HOOK_NICK_LOSTNICK, &lua_onquit);
  deregisterhook(HOOK_NICK_PRE_LOSTNICK, &lua_onprequit);
  deregisterhook(HOOK_CHANNEL_DEOPPED, &lua_onop);
  deregisterhook(HOOK_CHANNEL_OPPED, &lua_onop);
  deregisterhook(HOOK_CHANNEL_DEVOICED, &lua_onvoice);
  deregisterhook(HOOK_CHANNEL_VOICED, &lua_onvoice);
  deregisterhook(HOOK_CHANNEL_KICK, &lua_onkick);
  deregisterhook(HOOK_CHANNEL_TOPIC, &lua_ontopic);
  deregisterhook(HOOK_NICK_ACCOUNT, &lua_onauth);
  deregisterhook(HOOK_IRC_PRE_DISCON, &lua_ondisconnect);
  deregisterhook(HOOK_IRC_DISCON, &lua_ondisconnect);
  deregisterhook(HOOK_NICK_NEWNICK, &lua_onnewnick);
  deregisterhook(HOOK_CHANNEL_MODECHANGE, &lua_onmode);
  deregisterhook(HOOK_CHANNEL_JOIN_BYPASS_BAN, &lua_onbanevade);
}

void lua_startbot(void *arg) {
  channel *cp;

  myureconnect = NULL;

  if(!luabotnick)
    luabotnick = getcopyconfigitem("lua", "botnick", "U", NICKLEN);

  lua_nick = registerlocaluser(luabotnick->content, "lua", "quakenet.department.of.corrections", LUA_FULLVERSION, "U", UMODE_ACCOUNT | UMODE_DEAF | UMODE_OPER | UMODE_SERVICE, &lua_bothandler);
  if(!lua_nick) {
    myureconnect = scheduleoneshot(time(NULL) + 1, &lua_startbot, NULL);
    return;
  }

  cp = findchannel(LUA_OPERCHAN);
  if(cp && localjoinchannel(lua_nick, cp)) {
    localgetops(lua_nick, cp);
  } else {
    localcreatechannel(lua_nick, LUA_OPERCHAN);
  }

  cp = findchannel(LUA_PUKECHAN);
  if(cp && localjoinchannel(lua_nick, cp)) {
    localgetops(lua_nick, cp);
  } else {
    localcreatechannel(lua_nick, LUA_PUKECHAN);
  }

  myublip = schedulerecurring(time(NULL) + 1, 0, 60, &lua_blip, NULL);
  myutick = schedulerecurring(time(NULL) + 1, 0, 1, &lua_tick, NULL);

  lua_registerevents();
}

void lua_destroybot(void) {
  if(myutick)
    deleteschedule(myutick, &lua_tick, NULL);

  if(myublip)
    deleteschedule(myublip, &lua_blip, NULL);

  lua_deregisterevents();

  if (myureconnect)
    deleteschedule(myureconnect, &lua_startbot, NULL);

  if(lua_nick)
    deregisterlocaluser(lua_nick, NULL);

  if(luabotnick) {
    freesstring(luabotnick);
    luabotnick = NULL;
  }
}

int _lua_vpcall(lua_State *l, void *function, int mode, const char *sig, ...) {
  va_list va;
  int narg = 0, nres, top = lua_gettop(l);

  lua_getglobal(l, "scripterror");
  if(mode == LUA_CHARMODE) {
    lua_getglobal(l, (const char *)function);
  } else {
    lua_rawgeti(l, LUA_REGISTRYINDEX, (long)function);
  }

  if(!lua_isfunction(l, -1)) {
    lua_settop(l, top);
    return 1;
  }

  va_start(va, sig);

  while(*sig) {
    switch(*sig++) {
      case 'i':
        lua_pushint(l, va_arg(va, int));
        break;
      case 'l':
        lua_pushlong(l, va_arg(va, long));
        break;
      case 's':
        lua_pushstring(l, va_arg(va, char *));
        break;
      case 'S':
        lua_pushstring(l, ((sstring *)(va_arg(va, sstring *)))->content);
        break;
      case 'L': /* BE VERY CAREFUL USING THIS, MAKE SURE YOU CAST THE VALUE TO LONG */
        {
          char *p = va_arg(va, char *);
          long len = va_arg(va, long);
          lua_pushlstring(l, p, len);
        }
        break;
      case 'N':
        {
          nick *np = va_arg(va, nick *);
          lua_pushnumeric(l, np->numeric);
          break;
        }
      case 'C':
        {
          channel *cp = va_arg(va, channel *);
          LUA_PUSHCHAN(l, cp);
          break;
        }
      case '0':
        lua_pushnil(l);
        break;
      case 'R':
        lua_rawgeti(l, LUA_REGISTRYINDEX, va_arg(va, long));
        break;
      case 'b':
        lua_pushboolean(l, va_arg(va, int));
        break;
      case '>':
        goto endwhile;

      default:
        Error("lua", ERR_ERROR, "Unable to parse vpcall signature (%c)", *(sig - 1));
    }
    narg++;
  }

endwhile:

  nres = strlen(sig);

  if(lua_debugpcall(l, (mode==LUA_CHARMODE)?function:"some_handler", narg, nres, top + 1)) {
    Error("lua", ERR_ERROR, "Error pcalling %s: %s.", (mode==LUA_CHARMODE)?(char *)function:"some_handler", lua_tostring(l, -1));
  } else {
    nres = -nres;
    while(*sig) {
      switch(*sig++) {
        default:
          Error("lua", ERR_ERROR, "Unable to parse vpcall return structure (%c)", *(sig - 1));
      }
      nres++;
    }
  }

  lua_settop(l, top);
  va_end(va);
  return 0;
}

void lua_bothandler(nick *target, int type, void **args) {
  nick *np;
  char *p;
  int le;

  switch(type) {
    case LU_PRIVMSG:
      np = (nick *)args[0];
      p = (char *)args[1];

      if(!np || !p)
        return;

      lua_avpcall("irc_onmsg", "ls", np->numeric, p);

      break;
    case LU_PRIVNOTICE:
      np = (nick *)args[0];
      p = (char *)args[1];

      if(!np || !p)
        return;

      le = strlen(p);
      if((le > 1) && (p[0] == '\001')) {
        if(p[le - 1] == '\001')
          p[le - 1] = '\000';

        lua_avpcall("irc_onctcp", "ls", np->numeric, p + 1);
      } else {
        lua_avpcall("irc_onnotice", "ls", np->numeric, p);
      }

      break;

    case LU_KILLED:
      if(myublip) {
        myublip = NULL;
        deleteschedule(myublip, &lua_blip, NULL);
      }

      if(myutick) {
        myutick = NULL;
        deleteschedule(myutick, &lua_tick, NULL);
      }

      lua_nick = NULL;
      myureconnect = scheduleoneshot(time(NULL) + 1, &lua_startbot, NULL);

      break;
  }
}

void lua_blip(void *arg) {
  lua_avpcall("onblip", "");
}

void lua_tick(void *arg) {
  lua_avpcall("ontick", "");
}

void lua_onnewnick(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onnewnick", "l", np->numeric);
}

void lua_onkick(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *kicked = arglist[1];
  nick *kicker = arglist[2];
  char *message = (char *)arglist[3];

  if(kicker)
    lua_avpcall("irc_onkick", "Slls", ci->name, kicked->numeric, kicker->numeric, message);
  else
    lua_avpcall("irc_onkick", "Sl0s", ci->name, kicked->numeric, message);
}

void lua_ontopic(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  channel *cp=(channel*)arglist[0];
  nick *np = (nick *)arglist[1];

  if(!cp || !cp->topic)
    return;

  if(np)
    lua_avpcall("irc_ontopic", "SlS", cp->index->name, np->numeric, cp->topic);
  else
    lua_avpcall("irc_ontopic", "S0S", cp->index->name, cp->topic);
}

void lua_onop(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];
  nick *target = arglist[2];

  if(!target)
    return;

  if(np) {
    lua_avpcall(hooknum == HOOK_CHANNEL_OPPED?"irc_onop":"irc_ondeop", "Sll", ci->name, np->numeric, target->numeric);
  } else {
    lua_avpcall(hooknum == HOOK_CHANNEL_OPPED?"irc_onop":"irc_ondeop", "S0l", ci->name, target->numeric);
  }
}

void lua_onvoice(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];
  nick *target = arglist[2];

  if(!target)
    return;

  if(np) {
    lua_avpcall(hooknum == HOOK_CHANNEL_VOICED?"irc_onvoice":"irc_ondevoice", "Sll", ci->name, np->numeric, target->numeric);
  } else {
    lua_avpcall(hooknum == HOOK_CHANNEL_VOICED?"irc_onvoice":"irc_ondevoice", "S0l", ci->name, target->numeric);
  }
}

void lua_onjoin(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];

  if(!ci || !np)
    return;

  lua_avpcall("irc_onjoin", "Sl", ci->name, np->numeric);
}

void lua_onpart(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];
  char *reason = arglist[2];

  if(!ci || !np)
    return;

  lua_avpcall("irc_onpart", "Sls", ci->name, np->numeric, reason);
}
void lua_onbanevade(int hooknum, void *arg) {
  void **arglist = (void**)arg;
  channel *c = (channel*)arglist[0];
  nick *np = (nick*)arglist[1];

  /* Validate channel, channel index and nick */
  if(!c || !c->index || !np)
    return;

  lua_avpcall("irc_onbanevade", "Sl", c->index->name, np->numeric);
}
void lua_onrename(int hooknum, void *arg) {
  void **harg = (void **)arg;
  nick *np = harg[0];
  char *oldnick = harg[1];

  if(!np)
    return;

  lua_avpcall("irc_onrename", "ls", np->numeric, oldnick);
}

void lua_onquit(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onquit", "l", np->numeric);
}

void lua_onprequit(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onprequit", "l", np->numeric);
}

void lua_onauth(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onauth", "l", np->numeric);
}

void lua_ondisconnect(int hooknum, void *arg) {
  lua_avpcall(hooknum == HOOK_IRC_DISCON?"irc_ondisconnect":"irc_onpredisconnect", "");
}

void lua_onconnect(int hooknum, void *arg) {
  lua_avpcall(hooknum == HOOK_IRC_CONNECTED?"irc_onconnect":"irc_onendofburst", "");
}

void lua_onload(lua_State *l) {
  lua_vpcall(l, "onload", "");
}

void lua_onunload(lua_State *l) {
  lua_vpcall(l, "onunload", "");
}

int lua_channelmessage(channel *cp, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendmessagetochannel(lua_nick, cp, "%s", buf);

  return 0;
}

int lua_message(nick *np, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendmessagetouser(lua_nick, np, "%s", buf);

  return 0;
}

int lua_notice(nick *np, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendnoticetouser(lua_nick, np, "%s", buf);

  return 0;
}

char *printallmodes(channel *cp) {
  static char buf[1024];
  char buf2[12];

  if(IsLimit(cp))
    snprintf(buf2, sizeof(buf2), "%d", cp->limit);

  snprintf(buf, sizeof(buf), "%s %s%s%s", printflags(cp->flags, cmodeflags), IsLimit(cp)?buf2:"", IsLimit(cp)?" ":"", IsKey(cp)?cp->key->content:"");

  return buf;
}

static char *printlimitedmodes(channel *cp, flag_t before) {
  static char buf[1024];

  snprintf(buf, sizeof(buf), "%s", printflags(cp->flags, cmodeflags));

  return buf;
}

void lua_onmode(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  channel *cp = (channel *)arglist[0];
  chanindex *ci = cp->index;
  nick *np = arglist[1];
  flag_t beforeflags = (flag_t)(long)arglist[3];

  if(np) {
    lua_avpcall("irc_onmode", "Slss", ci->name, np->numeric, printallmodes(cp), printlimitedmodes(cp, beforeflags));
  } else {
    lua_avpcall("irc_onmode", "S0ss", ci->name, printallmodes(cp), printlimitedmodes(cp, beforeflags));
  }
}
