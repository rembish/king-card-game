#include "res.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSPR   72
#define NTRANS 17

typedef struct {
    int w, h;
    unsigned char *px; /* w * h colour indices */
} sprite;

static sprite spr[NSPR];
static unsigned char fonts[3][256 * 14];
static const int font_rows[3] = { 6, 8, 14 };
static SDL_Renderer *ren;
static SDL_Texture *tex[NSPR][NTRANS];
static SDL_Texture *font_tex[3];
static char err[256];

/* EGA palette registers (6-bit rgbRGB) after init_graphics: 4 = $14, 5 = $27, 6 = $2e */
static int pal[16] = { 0, 1, 2, 3, 0x14, 0x27, 0x2e, 7, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3b, 0x3e, 0x3f };

SDL_Color res_color(int ega)
{
    int v = pal[ega & 15];
    int r = ((v >> 2) & 1) * 2 + ((v >> 5) & 1), g = ((v >> 1) & 1) * 2 + ((v >> 4) & 1);
    int b = (v & 1) * 2 + ((v >> 3) & 1);
    return (SDL_Color){ (Uint8)(r * 85), (Uint8)(g * 85), (Uint8)(b * 85), 255 };
}

void res_set_palette(int index, int ega6)
{
    if (pal[index & 15] == ega6) return;
    pal[index & 15] = ega6;
    for (int i = 0; i < NSPR; i++)
        for (int t = 0; t < NTRANS; t++)
            if (tex[i][t]) {
                SDL_DestroyTexture(tex[i][t]);
                tex[i][t] = NULL;
            }
}

const char *res_error(void) { return err; }

static unsigned rd16(const unsigned char *p) { return (unsigned)(p[0] | p[1] << 8); }

int res_load(const unsigned char *lib, size_t lib_len, const unsigned char *fnt, size_t fnt_len)
{
    if (fnt_len != 7168) {
        snprintf(err, sizeof err, "KING.FNT: %zu bytes, expected 7168", fnt_len);
        return -1;
    }
    size_t off = 0;
    for (int f = 0; f < 3; f++) {
        memcpy(fonts[f], fnt + off, (size_t)256 * font_rows[f]);
        off += (size_t)256 * font_rows[f];
    }
    if (lib_len < 128 || lib[0] != NSPR) {
        snprintf(err, sizeof err, "KING.LIB: not the sprite library of KING");
        return -1;
    }
    off = 128;
    for (int k = 0; k < NSPR; k++) {
        size_t size = (size_t)lib[1 + k] * 128;
        free(spr[k].px);
        spr[k] = (sprite){ 0, 0, NULL };
        if (!size) continue;
        if (off + size > lib_len) {
            snprintf(err, sizeof err, "KING.LIB: truncated");
            return -1;
        }
        const unsigned char *s = lib + off, *end = lib + off + size;
        int w = (int)rd16(s), h = (int)rd16(s + 2);
        unsigned char *px = calloc((size_t)w * h, 1);
        const unsigned char *p = s + 6;
        for (int y = 0; y < h && p + 2 <= end; y++) {
            const unsigned char *q = p + 2, *row_end = p + 2 + rd16(p);
            int x = 0;
            while (q < row_end && q < end) {
                int c = *q++;
                if (c & 0x80) {
                    for (int n = c - 0x80; n > 0; n--, x++)
                        if (x < w) px[y * w + x] = *q;
                    q++;
                } else
                    for (; c > 0; c--, x++, q++)
                        if (x < w) px[y * w + x] = *q;
            }
            p = row_end;
        }
        spr[k] = (sprite){ w, h, px };
        off += size;
    }
    return 0;
}

static unsigned char *slurp(const char *dir, const char *name, size_t *len)
{
    char path[1100], up[64], lo[64];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; i++) {
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
        lo[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    }
    const char *variants[2] = { up, lo };
    for (int v = 0; v < 2; v++) {
        snprintf(path, sizeof path, "%s/%s", dir, variants[v]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        unsigned char *buf = malloc(1 << 20);
        *len = fread(buf, 1, 1 << 20, f);
        fclose(f);
        return buf;
    }
    return NULL;
}

int res_load_dir(const char *dir)
{
    size_t ll = 0, fl = 0;
    unsigned char *lib = slurp(dir, "KING.LIB", &ll), *fnt = slurp(dir, "KING.FNT", &fl);
    int r = -1;
    if (!lib || !fnt)
        snprintf(err, sizeof err, "KING.LIB and KING.FNT not found in %s", dir);
    else
        r = res_load(lib, ll, fnt, fl);
    free(lib);
    free(fnt);
    return r;
}

void res_init(SDL_Renderer *r)
{
    ren = r;
    for (int f = 0; f < 3; f++) {
        int rows = font_rows[f];
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 256 * 8, rows, 32, SDL_PIXELFORMAT_RGBA32);
        Uint32 *px = s->pixels;
        for (int g = 0; g < 256; g++)
            for (int y = 0; y < rows; y++) {
                int bits = fonts[f][g * rows + y];
                for (int x = 0; x < 8; x++)
                    px[y * (s->pitch / 4) + g * 8 + x] = (bits & (0x80 >> x)) ? 0xffffffffu : 0;
            }
        if (font_tex[f]) SDL_DestroyTexture(font_tex[f]);
        font_tex[f] = SDL_CreateTextureFromSurface(r, s);
        SDL_SetTextureBlendMode(font_tex[f], SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(s);
    }
}

int res_sprite_w(int id) { return id >= 0 && id < NSPR ? spr[id].w : 0; }
int res_sprite_h(int id) { return id >= 0 && id < NSPR ? spr[id].h : 0; }

static SDL_Texture *sprite_tex(int id, int transparent)
{
    if (id < 0 || id >= NSPR || !spr[id].px) return NULL;
    int t = transparent < 0 || transparent >= NTRANS ? SPR_NONE : transparent;
    if (tex[id][t]) return tex[id][t];
    sprite *s = &spr[id];
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, SDL_PIXELFORMAT_RGBA32);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int c = s->px[y * s->w + x];
            SDL_Color col = res_color(c);
            Uint8 *p = (Uint8 *)surf->pixels + y * surf->pitch + x * 4;
            p[0] = col.r;
            p[1] = col.g;
            p[2] = col.b;
            p[3] = (c & 15) == t ? 0 : 255;
        }
    tex[id][t] = SDL_CreateTextureFromSurface(ren, surf);
    SDL_SetTextureBlendMode(tex[id][t], SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
    return tex[id][t];
}

void res_sprite(int id, int x, int y, int transparent)
{
    SDL_Texture *t = sprite_tex(id, transparent);
    if (!t) return;
    SDL_Rect d = { x, y, spr[id].w, spr[id].h };
    SDL_RenderCopy(ren, t, NULL, &d);
}

void res_sprite_part(int id, int sx, int sy, int w, int h, int x, int y, int transparent)
{
    SDL_Texture *t = sprite_tex(id, transparent);
    if (!t) return;
    SDL_Rect s = { sx, sy, w, h }, d = { x, y, w, h };
    SDL_RenderCopy(ren, t, &s, &d);
}

/* next code point of a UTF-8 string, in CP866 (unknown characters become '?') */
static int cp866(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned u = *s++;
    if (u >= 0xc0 && (*s & 0xc0) == 0x80) {
        if (u < 0xe0)
            u = (u & 0x1f) << 6 | (*s++ & 0x3f);
        else {
            u = (u & 0x0f) << 12 | (s[0] & 0x3f) << 6 | (s[1] & 0x3f);
            s += 2;
        }
    }
    *ps = (const char *)s;
    if (u < 0x80) return (int)u;
    if (u >= 0x410 && u <= 0x42f) return (int)(0x80 + u - 0x410);
    if (u >= 0x430 && u <= 0x43f) return (int)(0xa0 + u - 0x430);
    if (u >= 0x440 && u <= 0x44f) return (int)(0xe0 + u - 0x440);
    if (u == 0x401) return 0xf0;
    if (u == 0x451) return 0xf1;
    return '?';
}

int res_text_len(const char *s)
{
    int n = 0;
    while (*s) {
        cp866(&s);
        n++;
    }
    return n;
}

void res_text(int font, int x, int y, int ega, int advance, const char *s)
{
    SDL_Texture *t = font_tex[font];
    if (!t) return;
    SDL_Color c = res_color(ega);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    int rows = font_rows[font];
    while (*s) {
        int g = cp866(&s);
        SDL_Rect src = { g * 8, 0, 8, rows }, dst = { x, y, 8, rows };
        SDL_RenderCopy(ren, t, &src, &dst);
        x += advance;
    }
}

void res_textf(int font, int x, int y, int ega, int advance, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    res_text(font, x, y, ega, advance, buf);
}

void res_text_shadow(int font, int x, int y, int ega, int shadow, int advance, const char *s)
{
    res_text(font, x + 1, y + 1, shadow, advance, s);
    res_text(font, x, y, ega, advance, s);
}

void res_fill(int x, int y, int w, int h, int ega)
{
    SDL_Color c = res_color(ega);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(ren, &r);
}

void res_frame(int x, int y, int w, int h, int ega)
{
    SDL_Color c = res_color(ega);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderDrawRect(ren, &r);
}

void res_box(int x, int y, int w, int h, int ega)
{
    res_fill(x, y, w, h, ega);
    res_fill(x, y, w, 1, (ega + 8) & 15);
    res_fill(x, y, 1, h, (ega + 8) & 15);
    res_fill(x, y + h - 1, w, 1, 8);
    res_fill(x + w - 1, y, 1, h, 8);
    res_fill(x + 1, y + h - 2, w - 1, 1, 8);
    res_fill(x + w - 2, y + 1, 1, h - 1, 8);
}
