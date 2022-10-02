/*
  Geoip module
  Copyright (C) 2004-2006 Chris Porter.
*/

#include "../nick/nick.h"
#include "../core/error.h"
#include "../core/config.h"
#include "../core/hooks.h"
#include "../control/control.h"
#include "../lib/version.h"

#include <strings.h>

#include <GeoIP.h>
#include "geoip.h"

MODULE_VERSION("");

int geoip_totals[COUNTRY_MAX + 1];
static int geoip_nickext = -1;
static GeoIP *gi = NULL;

static void geoip_setupuser(nick *np);

static void geoip_new_nick(int hook, void *args);
static void geoip_quit(int hook, void *args);
static void geoip_whois_handler(int hooknum, void *arg);

void _init(void) {
  int i;
  nick *np;
  sstring *filename;

  filename = getcopyconfigitem("geoip", "db", "GeoIP.dat", 256);

  gi = GeoIP_open(filename->content, GEOIP_MEMORY_CACHE);
  if(!gi) {
    Error("geoip", ERR_WARNING, "Unable to load geoip database [filename: %s]", filename->content);
    freesstring(filename);
    return;
  }
  freesstring(filename);

  geoip_nickext = registernickext("geoip");
  if(geoip_nickext == -1)
    return; /* PPA: registerchanext produces an error, however the module should stop loading */

  memset(geoip_totals, 0, sizeof(geoip_totals));

  for(i=0;i<NICKHASHSIZE;i++)
    for(np=nicktable[i];np;np=np->next)
      geoip_setupuser(np);

  registerhook(HOOK_NICK_LOSTNICK, &geoip_quit);
  registerhook(HOOK_NICK_NEWNICK, &geoip_new_nick);
  registerhook(HOOK_CONTROL_WHOISREQUEST, &geoip_whois_handler);
}

void _fini(void) {
  if(gi)
    GeoIP_delete(gi);

  if(geoip_nickext == -1)
    return;

  releasenickext(geoip_nickext);

  deregisterhook(HOOK_NICK_NEWNICK, &geoip_new_nick);
  deregisterhook(HOOK_NICK_LOSTNICK, &geoip_quit);
  deregisterhook(HOOK_CONTROL_WHOISREQUEST, &geoip_whois_handler);
}

static void geoip_setupuser(nick *np) {
  if (!irc_in_addr_is_ipv4(&np->ipaddress)) 
    return; /* geoip only supports ipv4 */

  unsigned int ip = irc_in_addr_v4_to_int(&np->ipaddress);
  int country = GeoIP_id_by_ipnum(gi, ip);
  if((country < COUNTRY_MIN) || (country > COUNTRY_MAX))
    return;

  geoip_totals[country]++;
  np->exts[geoip_nickext] = (void *)(long)country;
}

static void geoip_new_nick(int hook, void *args) {
  geoip_setupuser((nick *)args);
}

static void geoip_quit(int hook, void *args) {
  int item;
  nick *np = (nick *)args;

  item = (int)((long)np->exts[geoip_nickext]);
  
  if((item < COUNTRY_MIN) || (item > COUNTRY_MAX))
    return;

  geoip_totals[item]--;
}

static void geoip_whois_handler(int hooknum, void *arg) {
  int item;
  char message[512];
  const char *longcountry, *shortcountry;
  nick *np = (nick *)arg;

  if(!np)
    return;

  item = (int)((long)np->exts[geoip_nickext]);
  if((item < COUNTRY_MIN) || (item > COUNTRY_MAX))
    return;

  shortcountry = GeoIP_code_by_id(item);
  longcountry = GeoIP_name_by_id(item);

  if(shortcountry && longcountry) {
    snprintf(message, sizeof(message), "Country   : %s (%s)", shortcountry, longcountry);
    triggerhook(HOOK_CONTROL_WHOISREPLY, message);
  }
}

int geoip_lookupcode(char *code) {
  int i;
  for(i=COUNTRY_MIN;i<COUNTRY_MAX;i++)
    if(!strcasecmp(code, GeoIP_country_code[i]))
      return i;

  return -1;
}
