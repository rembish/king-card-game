/* Replay a game through the core and print one JSON record per deal, in the form
 * re/emu/record.py stores the original's (tests/difftest.py compares them).
 *
 *     replay SEED POLLS        POLLS: one character per keyboard poll (kg_poll_token)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kg_core.h"

static const char *polls;

static int next_key(void)
{
    int k = *polls ? kg_poll_key(*polls++) : -1;
    if (k < 0) {
        fprintf(stderr, "poll script exhausted or bad\n");
        exit(2);
    }
    return k;
}

static void print_hand(const kg_game *g, int p)
{
    printf("\"%d\":[", p);
    for (int i = 1; i <= g->hands[p][0]; i++) printf("%s%d", i > 1 ? "," : "", g->hands[p][i]);
    printf("]%s", p < 4 ? "," : "");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: replay SEED POLLS\n");
        return 2;
    }
    static kg_game game;
    kg_game *g = &game;
    kg_new_game(g, (uint32_t)strtoul(argv[1], 0, 0));
    polls = argv[2];
    while (!kg_game_over(g)) {
        printf("{\"seed\":%u,\"dealer\":%d,", g->seed, g->dealer);
        kg_deal(g);
        int decl = kg_declarer(g);
        if (decl == KG_HUMAN) {
            kg_contract_begin(g);
            while (!kg_contract_poll(g, next_key())) {}
        } else
            kg_ai_contract(g);
        printf("\"hands\":{");
        for (int p = 1; p <= 4; p++) print_hand(g, p);
        printf("},\"game\":%d,\"declarer\":%d,\"seed_play\":%u,\"tricks\":[", g->deal_no, decl, g->seed);
        kg_start_play(g);
        int n = 0;
        while (!kg_deal_done(g)) {
            int p = kg_turn(g);
            if (n >= KG_CARDS || g->hands[p][0] < 1) {
                fprintf(stderr, "impossible state at play %d\n", n);
                return 3;
            }
            if (p == KG_HUMAN) {
                kg_human_begin(g);
                while (!kg_human_poll(g, next_key())) {}
            } else {
                int i = kg_ai_choose(g, p);
                if (i < 1 || i > g->hands[p][0]) {
                    fprintf(stderr, "computer chose card %d of %d\n", i, g->hands[p][0]);
                    return 3;
                }
                kg_play(g, p, i);
            }
            if (n % 4 == 0) printf("%s{\"plays\":[", n ? "," : "");
            printf("%s[%d,%d]", n % 4 ? "," : "", p, g->hands[p][9]);
            kg_after_card(g);
            if (++n % 4 == 0) printf("],\"winner\":%d}", g->last_winner);
        }
        printf("],\"deal_score\":[%d,%d,%d,%d],", g->deal_score[1], g->deal_score[2], g->deal_score[3],
               g->deal_score[4]);
        kg_end_deal(g);
        printf("\"totals\":[%d,%d,%d,%d],\"seed_end\":%u}\n", g->total[1], g->total[2], g->total[3],
               g->total[4], g->seed);
    }
    if (*polls) fprintf(stderr, "%zu polls left over\n", strlen(polls));
    return 0;
}
