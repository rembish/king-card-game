/* The global club at king.rembi.sh (cloudflare/): the browser build only. Every call starts a
 * request and returns at once; the *_status calls say how it went. Natively all of it is off. */
#ifndef NET_H
#define NET_H

#include <stdint.h>

enum { NET_PENDING, NET_OK, NET_TAKEN, NET_FAILED };

void net_claim(const char *name);
int net_claim_status(void);

void net_new_game(void);
/* the server's seed for the next game, if it came */
int net_game_seed(uint32_t *seed);

void net_submit(const char *name, const char *polls);
/* NET_OK: buf = "members total balance games new\n" then "balance games name\n" per top member;
 * NET_FAILED / NET_TAKEN: buf = the reason */
int net_result(char *buf, int max);

#endif
