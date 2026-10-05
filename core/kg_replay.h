/* Replaying a whole game from its seed and its keyboard polls, as the frontend plays it: what
 * the server runs to check a submitted game, and what the tests run. */
#ifndef KG_REPLAY_H
#define KG_REPLAY_H

#include <stddef.h>
#include "kg_core.h"

enum {
    KG_REPLAY_OK,
    KG_REPLAY_SHORT,    /* the polls ran out before the game ended */
    KG_REPLAY_BAD_POLL, /* a character that is not a poll token */
    KG_REPLAY_LEFTOVER, /* polls left after the game ended */
};

/* Plays the game in g from `seed`: the human's contract grid and card choices take one poll
 * each from `polls` (kg_poll_token characters), the computer plays as the original. */
int kg_replay(kg_game *g, uint32_t seed, const char *polls, size_t n);

#endif
