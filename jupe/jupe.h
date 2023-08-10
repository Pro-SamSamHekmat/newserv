typedef struct jupe_s {
	struct jupe_s*	ju_next;
	sstring*	ju_server;
	sstring*	ju_reason;
	time_t		ju_expire;
	time_t		ju_lastmod;
	unsigned int	ju_flags;
} jupe_t;

extern jupe_t *jupes;

#define JUPE_MAX_EXPIRE	604800

#define JUPE_ACTIVE	0x0001

#define JupeIsRemActive(j)	((j)->ju_flags & JUPE_ACTIVE)

#define JupeServer(j)		((j)->ju_server->content)
#define JupeReason(j)		((j)->ju_reason->content)
#define JupeLastMod(j)		((j)->ju_lastmod)

void jupe_propagate(jupe_t *jupe);
jupe_t *make_jupe(char *server, char *reason, time_t expirets, time_t lastmod, unsigned int flags);
void jupe_free(jupe_t *jupe);

/* (public) functions for using/modifying jupes */
jupe_t *jupe_find(char *server);
void jupe_activate(jupe_t *jupe);
void jupe_deactivate(jupe_t *jupe);
int jupe_add(char *server, char *reason, time_t duration, unsigned int flags);
void jupe_expire(void); /* call this before directly using the jupes list */
