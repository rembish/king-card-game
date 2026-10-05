#include "net.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
EM_JS(void, js_net_init, (void), {
    if (Module.kingNet) return;
    var id = null;
    try {
        id = localStorage.getItem('king:id');
        if (!id) { id = crypto.randomUUID(); localStorage.setItem('king:id', id); }
    } catch (e) { id = crypto.randomUUID(); }
    Module.kingNet = { id: id, claim: 0, game: null, result: 0, text: '' };
});

EM_JS(void, js_net_post, (const char *path, const char *body, int what), {
    var net = Module.kingNet, p = UTF8ToString(path), b = JSON.parse(UTF8ToString(body));
    b.id = net.id;
    if (what === 2) b.token = net.token;
    fetch(p, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(b) })
        .then(function (r) { return r.json().then(function (j) { return [r.status, j]; }); })
        .then(function (x) {
            var st = x[0], j = x[1];
            if (what === 0) net.claim = st === 200 ? 1 : st === 409 ? 2 : 3;
            if (what === 1 && st === 200) { net.game = j.seed >>> 0; net.token = j.token; }
            if (what === 2) {
                if (st !== 200) { net.result = st === 409 ? 2 : 3; net.text = j.error || ('HTTP ' + st); return; }
                var t = j.members + ' ' + j.total + ' ' + j.balance + ' ' + j.games + ' ' + (j.isNew ? 1 : 0) + '\n';
                j.top.forEach(function (r) { t += r.balance + ' ' + r.games + ' ' + r.name + '\n'; });
                net.text = t;
                net.result = 1;
            }
        })
        .catch(function (e) {
            if (what === 0) net.claim = 3;
            if (what === 2) { net.result = 3; net.text = String(e); }
        });
});

EM_JS(int, js_net_get, (int what), {
    var net = Module.kingNet;
    return what === 0 ? net.claim : net.result;
});

EM_JS(int, js_net_seed, (uint32_t *seed), {
    var net = Module.kingNet;
    if (net.game === null) return 0;
    HEAPU32[seed >> 2] = net.game;
    net.game = null;
    return 1;
});

EM_JS(void, js_net_text, (char *buf, int max), { stringToUTF8(Module.kingNet.text, buf, max); });

EM_JS(void, js_net_reset, (int what), {
    var net = Module.kingNet;
    if (what === 0) net.claim = 0; else { net.result = 0; net.text = ''; }
});
/* clang-format on */

static void body(char *out, int max, const char *name, const char *polls)
{
    /* names are letters, digits and punctuation (the game's input), polls are token letters:
     * only quotes and backslashes need escaping */
    int n = 0;
    const char *parts[2] = { name, polls };
    const char *keys[2] = { "name", "polls" };
    n += snprintf(out + n, (size_t)(max - n), "{");
    for (int k = 0; k < 2; k++) {
        if (!parts[k]) continue;
        n += snprintf(out + n, (size_t)(max - n), "%s\"%s\":\"", k && parts[0] ? "," : "", keys[k]);
        for (const char *s = parts[k]; *s && n < max - 4; s++) {
            if (*s == '"' || *s == '\\') out[n++] = '\\';
            out[n++] = *s;
        }
        out[n++] = '"';
    }
    snprintf(out + n, (size_t)(max - n), "}");
}

void net_claim(const char *name)
{
    char b[256];
    js_net_init();
    js_net_reset(0);
    body(b, sizeof b, name, NULL);
    js_net_post("/api/name", b, 0);
}
int net_claim_status(void) { return js_net_get(0); }

void net_new_game(void)
{
    js_net_init();
    js_net_post("/api/game", "{}", 1);
}
int net_game_seed(uint32_t *seed) { return js_net_seed(seed); }

void net_submit(const char *name, const char *polls)
{
    size_t max = strlen(polls) * 2 + 256;
    char *b = malloc(max);
    if (!b) return;
    js_net_reset(1);
    body(b, (int)max, name, polls);
    js_net_post("/api/result", b, 2);
    free(b);
}

int net_result(char *buf, int max)
{
    int r = js_net_get(1);
    if (r != NET_PENDING) js_net_text(buf, max);
    return r;
}

#else

void net_claim(const char *name) { (void)name; }
int net_claim_status(void) { return NET_FAILED; }
void net_new_game(void) {}
int net_game_seed(uint32_t *seed)
{
    *seed = 0;
    return 0;
}
void net_submit(const char *name, const char *polls)
{
    (void)name;
    (void)polls;
}
int net_result(char *buf, int max)
{
    if (max > 0) buf[0] = 0;
    return NET_FAILED;
}

#endif
