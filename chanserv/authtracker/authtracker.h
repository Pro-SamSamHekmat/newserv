#ifndef AUTHTRACKER_H
#define AUTHTRACKER_H

#include "../../nick/nick.h"
#include "../../dbapi/dbapi.h"

#include <time.h>

extern DBModuleIdentifier authtrackerdb;

#define AT_NETSPLIT	0	/* User lost in netsplit */
#define AT_RESTART	1	/* Dangling session found at restart */

/* authtracker_query.c */
void at_logquit(unsigned long userid, time_t accountts, time_t time, char *reason);
void at_lognewsession(unsigned int userid, nick *np);
void at_finddanglingsessions();

/* authtracker_db.c */
void at_lostnick(unsigned int numeric, unsigned long userid, time_t accountts, time_t losttime, int reason);
int at_foundnick(unsigned int numeric, unsigned long userid, time_t accountts);
int at_serverback(unsigned int server);
void at_flushghosts();
int at_dumpdb(void *source, int argc, char **argv);
int at_delinkdb(void *source, int argc, char **argv);

/* authtracker_hooks.c */
unsigned long at_getuserid(nick *np);
void at_hookinit();
void at_hookfini();

/* authtracker.c */
void at_dbloaded(int hooknum, void *arg);


#endif
