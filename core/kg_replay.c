#include "kg_replay.h"

int kg_replay(kg_game *g, uint32_t seed, const char *polls, size_t n)
{
    size_t i = 0;
    kg_new_game(g, seed);
    while (!kg_game_over(g)) {
        kg_deal(g);
        if (kg_declarer(g) == KG_HUMAN) {
            kg_contract_begin(g);
            for (;;) {
                if (i >= n) return KG_REPLAY_SHORT;
                int k = kg_poll_key(polls[i++]);
                if (k < 0) return KG_REPLAY_BAD_POLL;
                if (kg_contract_poll(g, k)) break;
            }
        } else
            kg_ai_contract(g);
        kg_start_play(g);
        while (!kg_deal_done(g)) {
            if (kg_turn(g) == KG_HUMAN) {
                kg_human_begin(g);
                for (;;) {
                    if (i >= n) return KG_REPLAY_SHORT;
                    int k = kg_poll_key(polls[i++]);
                    if (k < 0) return KG_REPLAY_BAD_POLL;
                    if (kg_human_poll(g, k)) break;
                }
            } else
                kg_ai_play(g);
            kg_after_card(g);
        }
        kg_end_deal(g);
    }
    return i == n ? KG_REPLAY_OK : KG_REPLAY_LEFTOVER;
}
