/* KING game logic, reconstructed from KING2.EXE (full variant, v1.1; D. Bashurov, 1993).
 *
 * Deterministic and free of I/O. One game = 56 deals: before each deal's play the declarer
 * (who also leads first) picks one of its 14 unplayed games. Player 4 is the human, 1..3 the
 * computer (1 left, 2 top, 3 right).
 *
 * Driving it, as the original's main loop does:
 *
 *     kg_new_game(g, seed);
 *     while (!kg_game_over(g)) {
 *         kg_deal(g);                                  shuffle and deal
 *         if (kg_declarer(g) == 4) { kg_contract_begin(g); while (!kg_contract_poll(g, key)); }
 *         else kg_ai_contract(g);
 *         kg_start_play(g);
 *         while (!kg_deal_done(g)) {
 *             if (kg_turn(g) == 4) { kg_human_begin(g); while (!kg_human_poll(g, key)); }
 *             else kg_ai_play(g);
 *             kg_after_card(g);                        a finished trick is scored here
 *         }
 *         kg_end_deal(g);
 *     }
 *
 * The human's routines are called once per keyboard poll of the original (one pass of its
 * input loop, 30 ms apart): the card cursor draws random numbers while it waits, so the deal
 * order after it depends on how many polls the human took. `key` is KG_KEY_NONE when no key
 * was pressed on that poll.
 */
#ifndef KG_CORE_H
#define KG_CORE_H

#include <stdint.h>

#define KG_PLAYERS 4
#define KG_HUMAN   4
#define KG_CARDS   32
#define KG_HAND    8
#define KG_GAMES   14 /* 7 contracts x {don't take, take} */

/* card = suit * 13 + rank; suit 0 diamonds, 1 clubs, 2 spades, 3 hearts; rank 0..12 = 2..A */
#define KG_SUIT(c)   ((c) / 13)
#define KG_RANK(c)   ((c) % 13)
#define KG_HEARTS    3
#define KG_KING_HEARTS 50

enum { KG_TRICKS, KG_HEARTS_C, KG_BOYS, KG_GIRLS, KG_LAST_TWO, KG_KING, KG_ALL };

/* keyboard polls */
enum { KG_KEY_NONE, KG_KEY_SPACE, KG_KEY_LEFT, KG_KEY_RIGHT, KG_KEY_UP, KG_KEY_DOWN, KG_KEY_OTHER };

/* Rows of words as the original keeps them (ds:06fa + 20p): [0] count, [1..8] cards in hand
 * order, [9] the card on the table. The computer's hands stay in deal order; the human's is
 * sorted. Order matters: the AI tries cards in hand order and keeps the first on ties. */
typedef int16_t kg_rows[KG_PLAYERS + 1][10];

typedef struct {
    uint32_t seed; /* Turbo Pascal RandSeed */

    uint8_t played[KG_PLAYERS + 1][KG_GAMES]; /* games_played */
    int16_t games_left[KG_PLAYERS + 1];
    int16_t deal_score[KG_PLAYERS + 1];
    int16_t total[KG_PLAYERS + 1];
    int16_t taken[KG_PLAYERS + 1]; /* items taken this deal (the counters on screen) */

    int16_t dealer;      /* ds:07b0: +1 per card dealt and per deal */
    int16_t leader;      /* ds:07b2: leader % 4 + 1 plays next */
    int16_t deal_no;     /* chosen game 0..13: % 7 contract, < 7 don't take */
    int16_t price;       /* per item, negative when not taking */
    int16_t items_left;  /* the deal ends when 0 */
    int16_t trick_no;    /* tricks completed this deal */
    int16_t in_trick;    /* cards on the table */
    int16_t trick_best;  /* best card + 256 * player, 0 before the first card */
    int16_t current;     /* player who just played or is to play */
    int16_t last_winner; /* of the last finished trick, 0 if none yet */
    kg_rows hands;

    /* the human's input loops */
    int16_t cursor;      /* card index 1..count */
    int16_t blink;       /* cursor blink phase, 0 = steady */
    int16_t must;        /* 0 any card, 1 must follow suit */
    int16_t no_hearts;   /* leading in 1/5/6 with another suit in hand */
    int16_t grid_half, grid_row;
} kg_game;

uint16_t kg_random(kg_game *g, uint16_t n);

void kg_new_game(kg_game *g, uint32_t seed);
int kg_game_over(const kg_game *g);

void kg_deal(kg_game *g);
int kg_declarer(const kg_game *g);
void kg_ai_contract(kg_game *g);
void kg_contract_begin(kg_game *g);
int kg_contract_poll(kg_game *g, int key); /* 1 when chosen */
void kg_set_contract(kg_game *g, int player, int game);

void kg_start_play(kg_game *g);
int kg_deal_done(const kg_game *g);
int kg_turn(const kg_game *g);
void kg_human_begin(kg_game *g);
int kg_human_poll(kg_game *g, int key); /* 1 when a card was played */
void kg_ai_play(kg_game *g);
int kg_ai_choose(const kg_game *g, int p); /* index 1..count */
void kg_play(kg_game *g, int p, int idx);
void kg_after_card(kg_game *g);
void kg_end_deal(kg_game *g);

#endif
