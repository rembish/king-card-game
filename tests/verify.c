/* kg_replay from the command line: prints the four totals of the game (tests/frontend.py and
 * the server use the same function). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kg_replay.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: verify SEED POLLS\n");
        return 2;
    }
    static kg_game g;
    int r = kg_replay(&g, (uint32_t)strtoul(argv[1], 0, 0), argv[2], strlen(argv[2]));
    printf("%d %d %d %d %d\n", r, g.total[1], g.total[2], g.total[3], g.total[4]);
    return r;
}
