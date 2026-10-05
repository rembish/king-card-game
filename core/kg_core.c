/* KING game logic, reconstructed from KING2.EXE. Addresses are KING2's (see re/NOTES.md). */
#include "kg_core.h"

#include <string.h>

/* sets in the code segment: contracts where hearts may not be led (1000:0e68 = 0add = 1137),
 * the boys (J, K: 08e1, 1700) and the last two tricks (1720) */
static int no_heart_lead(int contract) { return contract == 1 || contract == 5 || contract == 6; }
static int is_boy(int rank) { return rank == 9 || rank == 11; }
static int is_last_two(int trick) { return trick == 7 || trick == 8; }

/* System.Random (1c05:098b): RandSeed = RandSeed * $08088405 + 1, result (seed >> 16) mod n */
uint16_t kg_random(kg_game *g, uint16_t n)
{
    g->seed = g->seed * 0x08088405u + 1u;
    return n ? (uint16_t)((g->seed >> 16) % n) : 0;
}

void kg_new_game(kg_game *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->seed = seed;
    for (int p = 1; p <= KG_PLAYERS; p++) g->games_left[p] = KG_GAMES;
}

int kg_game_over(const kg_game *g)
{
    int n = 0;
    for (int p = 1; p <= KG_PLAYERS; p++) n += g->games_left[p];
    return n == 0;
}

/* deal (685c) up to the contract choice */
void kg_deal(kg_game *g)
{
    int16_t deck[KG_CARDS];
    for (int p = 1; p <= KG_PLAYERS; p++) {
        g->taken[p] = 0;
        g->hands[p][0] = 0;
    }
    for (int i = 0; i < KG_CARDS; i++) deck[i] = (int16_t)(i % 8 + 5 + (i / 8) * 13);
    for (int k = 0; k < 1000; k++) {
        int a = kg_random(g, 32), b = kg_random(g, 32);
        int16_t t = deck[a];
        deck[a] = deck[b];
        deck[b] = t;
    }
    for (int i = 0; i < KG_CARDS; i++) {
        int p = g->dealer % 4 + 1;
        g->hands[p][++g->hands[p][0]] = deck[i];
        g->dealer++;
    }
    int16_t *h = g->hands[KG_HUMAN];
    for (int i = 2; i <= h[0]; i++)
        for (int j = i; j <= h[0]; j++)
            if (h[j] < h[i - 1]) {
                int16_t t = h[j];
                h[j] = h[i - 1];
                h[i - 1] = t;
            }
}

int kg_declarer(const kg_game *g) { return g->dealer % 4 + 1; }

/* contract_screen (60cd): price per item and number of items */
void kg_set_contract(kg_game *g, int player, int game)
{
    static const int16_t price[7] = {20, 20, 20, 40, 80, 160, 20};
    static const int16_t items[7] = {8, 8, 8, 4, 2, 1, 8};
    g->played[player][game] = 1;
    g->games_left[player]--;
    g->deal_no = (int16_t)game;
    g->price = game < 7 ? (int16_t)-price[game % 7] : price[game % 7];
    g->items_left = items[game % 7];
}

/* choose_contract (57bc), the computer's rule */
static int pick(kg_game *g, int p, int half, int lo, int n)
{
    int free = 0;
    for (int i = lo; i < lo + n; i++) free += !g->played[p][half * 7 + i];
    if (!free) return -1;
    int c;
    do c = lo + kg_random(g, (uint16_t)n);
    while (g->played[p][half * 7 + c]);
    return c;
}

void kg_ai_contract(kg_game *g)
{
    int p = kg_declarer(g), v = -68, row = -1, half = -1;
    for (int i = 1; i <= g->hands[p][0]; i++) v += g->hands[p][i] % 13;
    if (v < 0) {
        if (v > -10) row = pick(g, p, 0, 0, 3);
        if (row < 0) row = pick(g, p, 0, 3, 4);
        if (row < 0) row = pick(g, p, 0, 0, 7);
        if (row >= 0) half = 0;
    }
    if (half < 0) {
        if (v < 10) row = pick(g, p, 1, 0, 3);
        if (row < 0) row = pick(g, p, 1, 3, 4);
        if (row < 0) row = pick(g, p, 1, 0, 7);
        if (row >= 0) half = 1;
    }
    if (half < 0) {
        int k;
        do k = kg_random(g, 14);
        while (g->played[p][k]);
        row = k % 7;
        half = k / 7;
    }
    kg_set_contract(g, p, half * 7 + row);
}

void kg_contract_begin(kg_game *g) { g->grid_half = g->grid_row = 0; }

/* choose_contract (57bc) for the human: one pass of its loop */
int kg_contract_poll(kg_game *g, int key)
{
    if (key >= KG_KEY_PICK(0) && key < KG_KEY_PICK(KG_GAMES)) {
        g->grid_half = (int16_t)((key - KG_KEY_PICK(0)) / 7);
        g->grid_row = (int16_t)((key - KG_KEY_PICK(0)) % 7);
        key = KG_KEY_SPACE;
    }
    if (key == KG_KEY_LEFT && g->grid_half > 0) g->grid_half--;
    if (key == KG_KEY_RIGHT && g->grid_half < 1) g->grid_half++;
    if (key == KG_KEY_UP && g->grid_row > 0) g->grid_row--;
    if (key == KG_KEY_DOWN && g->grid_row < 6) g->grid_row++;
    if (key == KG_KEY_SPACE && !g->played[KG_HUMAN][g->grid_half * 7 + g->grid_row]) {
        kg_set_contract(g, KG_HUMAN, g->grid_half * 7 + g->grid_row);
        return 1;
    }
    return 0;
}

/* play_deal (6e1d) set-up; the declarer leads */
void kg_start_play(kg_game *g)
{
    g->leader = g->dealer;
    g->trick_no = g->in_trick = g->trick_best = 0;
    g->last_winner = 0;
    for (int p = 1; p <= KG_PLAYERS; p++) g->hands[p][9] = 0;
    g->current = (int16_t)kg_turn(g);
}

int kg_deal_done(const kg_game *g) { return g->items_left == 0; }
int kg_turn(const kg_game *g) { return g->leader % 4 + 1; }

/* play_card (6c02): take card idx out of p's hand onto the table */
void kg_play(kg_game *g, int p, int idx)
{
    int16_t *h = g->hands[p];
    int16_t card = h[idx];
    h[0]--;
    for (int i = idx; i <= h[0]; i++) h[i] = h[i + 1];
    h[9] = card;
    g->current = (int16_t)p;
}

/* score_trick (1740) */
static void score(kg_game *g, int w)
{
    int c = g->deal_no % 7, n = 0;
    const int16_t *t[5] = {0, &g->hands[1][9], &g->hands[2][9], &g->hands[3][9], &g->hands[4][9]};
    switch (c) {
    case KG_TRICKS: n = 1; break;
    case KG_HEARTS_C:
        for (int p = 1; p <= 4; p++) n += *t[p] > 39;
        break;
    case KG_BOYS:
        for (int p = 1; p <= 4; p++) n += is_boy(*t[p] % 13);
        break;
    case KG_GIRLS:
        for (int p = 1; p <= 4; p++) n += *t[p] % 13 == 10;
        break;
    case KG_LAST_TWO: n = is_last_two(g->trick_no); break;
    case KG_KING:
        for (int p = 1; p <= 4; p++) n += *t[p] == KG_KING_HEARTS;
        break;
    case KG_ALL:
        g->taken[w]++;
        g->items_left--;
        g->deal_score[w] += g->price;
        for (int p = 1; p <= 4; p++) {
            if (*t[p] > 39) g->deal_score[w] += g->price;
            if (is_boy(*t[p] % 13)) g->deal_score[w] += g->price;
            if (*t[p] % 13 == 10) g->deal_score[w] += (int16_t)(g->price * 2);
        }
        if (is_last_two(g->trick_no)) g->deal_score[w] += (int16_t)(g->price * 4);
        for (int p = 1; p <= 4; p++)
            if (*t[p] == KG_KING_HEARTS) g->deal_score[w] += (int16_t)(g->price * 8);
        return;
    }
    g->taken[w] += (int16_t)n;
    g->items_left -= (int16_t)n;
    g->deal_score[w] += (int16_t)(g->price * n);
}

/* the rest of play_deal's loop body after play_card; finish_trick (1a56) on the fourth card */
void kg_after_card(kg_game *g)
{
    int p = g->current;
    int16_t card = g->hands[p][9], best = (int16_t)(g->trick_best % 256);
    g->in_trick++;
    if (g->in_trick == 1 || (KG_SUIT(card) == KG_SUIT(best) && best < card))
        g->trick_best = (int16_t)(card + 256 * p);
    g->leader++;
    if (g->in_trick == 4) {
        g->trick_no++;
        int w = 0, hi = 0;
        best = (int16_t)(g->trick_best % 256);
        for (int q = 1; q <= 4; q++)
            if (KG_SUIT(g->hands[q][9]) == KG_SUIT(best) && hi < g->hands[q][9]) {
                hi = g->hands[q][9];
                w = q;
            }
        score(g, w);
        g->leader = (int16_t)(w - 1);
        g->last_winner = (int16_t)w;
        g->in_trick = 0;
        g->trick_best = 0;
        for (int q = 1; q <= 4; q++) g->hands[q][9] = 0;
    }
    g->current = (int16_t)kg_turn(g);
}

/* main, after deal_summary (3f36) */
void kg_end_deal(kg_game *g)
{
    for (int p = 1; p <= KG_PLAYERS; p++) {
        g->total[p] += g->deal_score[p];
        g->deal_score[p] = 0;
        g->hands[p][0] = 0;
    }
    g->dealer++;
}

/* human_choose_card (1157): entry */
void kg_human_begin(kg_game *g)
{
    const int16_t *h = g->hands[KG_HUMAN];
    int led = KG_SUIT(g->trick_best % 256);
    g->no_hearts = 0;
    if (g->trick_best == 0 && no_heart_lead(g->deal_no % 7))
        for (int i = 1; i <= h[0]; i++)
            if (KG_SUIT(h[i]) < KG_HEARTS) g->no_hearts = 1;
    g->must = 0;
    if (g->trick_best > 0)
        for (int i = 1; i <= h[0]; i++)
            if (KG_SUIT(h[i]) == led) g->must = 1;
    if (!g->must)
        g->cursor = (int16_t)(kg_random(g, (uint16_t)h[0]) + 1);
    else
        do g->cursor = (int16_t)(kg_random(g, (uint16_t)h[0]) + 1);
        while (KG_SUIT(h[g->cursor]) != led);
    g->blink = 0;
}

/* human_choose_card: one pass of its loop. The cursor blinks for 11 passes when
 * Random(20) = 19, drawn only on passes where it is not already blinking. */
int kg_human_poll(kg_game *g, int key)
{
    const int16_t *h = g->hands[KG_HUMAN];
    if (g->blink > 0) g->blink++;
    if (g->blink > 11) g->blink = 0;
    if (g->blink == 0 && kg_random(g, 20) == 19) g->blink = 1;
    if (key > KG_KEY_PICK(0) && key <= KG_KEY_PICK(h[0])) {
        g->cursor = (int16_t)(key - KG_KEY_PICK(0));
        key = KG_KEY_SPACE;
    }
    if (key == KG_KEY_LEFT && g->cursor - 1 > 0) g->cursor--;
    if (key == KG_KEY_RIGHT && g->cursor + 1 < h[0] + 1) g->cursor++;
    if (key != KG_KEY_SPACE) return 0;
    int ok = 0, led = KG_SUIT(g->trick_best % 256);
    if (!g->must && !g->no_hearts) ok = 1;
    if (g->no_hearts && KG_SUIT(h[g->cursor]) < KG_HEARTS) ok = 1;
    if (g->must && KG_SUIT(h[g->cursor]) == led) ok = 1;
    if (!ok) return 0;
    kg_play(g, KG_HUMAN, g->cursor);
    return 1;
}

/* ---- the computer ---- */

typedef struct {
    int16_t contract_game; /* deal_no */
    int depth, root;
} search_ctx;

/* ai_trick_value (0901): reads row[9] of each player as the trick's cards, but in the search
 * row[9] holds the running scores (see re/NOTES.md). Kept as it is. */
static int16_t trick_value(const search_ctx *x, kg_rows s)
{
    int c = x->contract_game % 7;
    int16_t v = 0;
    if (c == 0 || c == 4) v = 20;
    if (c == 1 || c == 6)
        for (int p = 1; p <= 4; p++)
            if (s[p][9] > 0x27) v += 20;
    if (c == 2 || c == 6)
        for (int p = 1; p <= 4; p++)
            if (is_boy(s[p][9] % 13)) v += 20;
    if (c == 3 || c == 6)
        for (int p = 1; p <= 4; p++)
            if (s[p][9] % 13 == 10) v += 40;
    if (c == 5 || c == 6)
        for (int p = 1; p <= 4; p++)
            if (s[p][9] == KG_KING_HEARTS) v += 160;
    if (c == 6) v += 20;
    return x->contract_game < 7 ? v : (int16_t)-v;
}

/* ai_search (0afd) */
static int16_t search(const search_ctx *x, int16_t best, int16_t tricks, int16_t ncards, int16_t q,
                      int16_t i, const kg_rows in)
{
    kg_rows s;
    memcpy(s, in, sizeof s);
    int16_t card = s[q][i];
    if (best == 0)
        best = (int16_t)(card + (q << 8));
    else if (KG_SUIT(card) == KG_SUIT(best & 0xff) && card > (best & 0xff))
        best = (int16_t)(card + (q << 8));
    s[q][0]--;
    for (int k = i; k <= s[q][0]; k++) s[q][k] = s[q][k + 1];
    int16_t w = 0;
    if (++ncards == 4) {
        w = (int16_t)((uint16_t)best >> 8);
        s[w][9] = (int16_t)(s[w][9] + trick_value(x, s));
        ncards = 0;
        tricks++;
        best = 0;
    }
    if (tricks == x->depth) return s[x->root][9];
    int led = -1;
    if (ncards > 0) {
        if (++q > 4) q = 1;
        for (int k = 1; k <= s[q][0]; k++)
            if (KG_SUIT(s[q][k]) == KG_SUIT(best & 0xff)) led = KG_SUIT(best & 0xff);
    } else
        q = w;
    int no_hearts = 0;
    if (ncards == 0 && no_heart_lead(x->contract_game % 7))
        for (int k = 1; k <= s[q][0]; k++)
            if (KG_SUIT(s[q][k]) < KG_HEARTS) no_hearts = 1;
    int16_t v = q == x->root ? 1000 : -1000;
    for (int k = 1; k <= s[q][0]; k++) {
        if (no_hearts && KG_SUIT(s[q][k]) >= KG_HEARTS) continue;
        if (led >= 0 && KG_SUIT(s[q][k]) != led) continue;
        int16_t r = search(x, best, tricks, ncards, q, (int16_t)k, s);
        if (q == x->root ? r < v : r > v) v = r;
    }
    return v;
}

/* ai_choose_card (0e88) */
int kg_ai_choose(const kg_game *g, int p)
{
    static const int depth_by_trick[8] = {2, 2, 2, 2, 3, 3, 2, 1};
    kg_rows s;
    memcpy(s, g->hands, sizeof s);
    search_ctx x = {g->deal_no, depth_by_trick[g->trick_no & 7], p};
    const int16_t *h = s[p];
    if (h[0] == 1) return 1;
    int no_hearts = 0, must = 0, best = g->trick_best % 256;
    if (g->in_trick == 0 && no_heart_lead(g->deal_no % 7))
        for (int i = 1; i <= h[0]; i++)
            if (KG_SUIT(h[i]) < KG_HEARTS) no_hearts = 1;
    if (g->in_trick > 0) {
        int hi_i = 0, hi = 0;
        for (int i = 1; i <= h[0]; i++)
            if (KG_SUIT(h[i]) == KG_SUIT(best) && h[i] > hi) {
                hi_i = i;
                hi = h[i];
            }
        if (hi_i > 0 && best > hi) return hi_i;
        if (hi_i > 0) must = 1;
    }
    for (int q = 1; q <= 4; q++) s[q][9] = 0;
    int pick_i = 0;
    int16_t pick_v = 1000;
    for (int i = 1; i <= h[0]; i++) {
        if (no_hearts && KG_SUIT(h[i]) >= KG_HEARTS) continue;
        if (must && KG_SUIT(h[i]) != KG_SUIT(best)) continue;
        int16_t v = search(&x, g->trick_best, 0, g->in_trick, (int16_t)p, (int16_t)i, s);
        if (v < pick_v) {
            pick_v = v;
            pick_i = i;
        }
    }
    return pick_i;
}

void kg_ai_play(kg_game *g)
{
    int p = kg_turn(g);
    kg_play(g, p, kg_ai_choose(g, p));
}
