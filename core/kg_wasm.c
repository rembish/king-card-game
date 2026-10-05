/* The core as a standalone WebAssembly module for the server (cloudflare/functions): the page
 * writes the polls into kg_wasm_buffer(), calls kg_wasm_verify and reads the totals. */
#include "kg_replay.h"

#define KG_WASM_MAX (1 << 21)

static char buffer[KG_WASM_MAX];
static kg_game game;

__attribute__((export_name("buffer"))) char *kg_wasm_buffer(void) { return buffer; }
__attribute__((export_name("capacity"))) int kg_wasm_capacity(void) { return KG_WASM_MAX; }
__attribute__((export_name("verify"))) int kg_wasm_verify(uint32_t seed, int len)
{
    if (len < 0 || len > KG_WASM_MAX) return KG_REPLAY_SHORT;
    return kg_replay(&game, seed, buffer, (size_t)len);
}
__attribute__((export_name("total"))) int kg_wasm_total(int p)
{
    return p >= 1 && p <= 4 ? game.total[p] : 0;
}
