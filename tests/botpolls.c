/* The polls of a whole game in which the human's seat plays like the computer (one pick per
 * turn): a valid submission for any seed, for testing the server.  botpolls SEED */
#include <stdio.h>
#include <stdlib.h>

#include "kg_core.h"

int main(int argc, char **argv)
{
    static kg_game g;
    kg_new_game(&g, argc > 1 ? (uint32_t)strtoul(argv[1], 0, 0) : 1);
    while (!kg_game_over(&g)) {
        kg_deal(&g);
        if (kg_declarer(&g) == KG_HUMAN) {
            int k = 0;
            while (g.played[KG_HUMAN][k]) k++;
            kg_contract_begin(&g);
            kg_contract_poll(&g, KG_KEY_PICK(k));
            putchar(kg_poll_token(KG_KEY_PICK(k)));
        } else
            kg_ai_contract(&g);
        kg_start_play(&g);
        while (!kg_deal_done(&g)) {
            if (kg_turn(&g) == KG_HUMAN) {
                int i = kg_ai_choose(&g, KG_HUMAN);
                kg_human_begin(&g);
                kg_human_poll(&g, KG_KEY_PICK(i));
                putchar(kg_poll_token(KG_KEY_PICK(i)));
            } else
                kg_ai_play(&g);
            kg_after_card(&g);
        }
        kg_end_deal(&g);
    }
    printf("\n%d\n", g.total[KG_HUMAN]);
    return 0;
}
