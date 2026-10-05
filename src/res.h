/* The original's pictures (KING.LIB) and fonts (KING.FNT), read at run time from the player's
 * own copy of the game. Nothing of them is part of this repository.
 *
 * Everything is drawn in the original's 640x350 EGA coordinates with its 16 colours. */
#ifndef KING_RES_H
#define KING_RES_H

#include <SDL.h>

#define RES_W 640
#define RES_H 350

/* sprite numbers in KING.LIB */
enum {
    SPR_BACK = 54,     /* card back */
    SPR_HAND = 55,     /* pointing hand (card cursor) */
    SPR_STRIP = 57,    /* 57..59: the partners, four faces each */
    SPR_MARS = 60,     /* marks an unplayed "take" game in the contract grid */
    SPR_SNICKERS = 61, /* marks an unplayed "don't take" game */
    SPR_HAND_BLINK = 62,
    SPR_LOGO_KP = 64, /* Komsomolskaya Pravda */
    SPR_CORNER = 65,  /* 65, 66: small ornaments */
    SPR_LOGO_KING = 67,
    SPR_FACES = 68,
    SPR_BILL = 69,
    SPR_FLAME = 70, /* 70, 71 */
};
#define SPR_NONE 16 /* "no transparent colour" */

enum { FONT_6, FONT_8, FONT_14 };

/* EGA colours by index (palette registers as the game sets them) */
SDL_Color res_color(int ega);
void res_set_palette(int index, int ega6); /* SetPalette(index, rgbRGB value) */

/* Load from the two files' contents; 0 on success, else an error message is in res_error(). */
int res_load(const unsigned char *lib, size_t lib_len, const unsigned char *fnt, size_t fnt_len);
/* Look for KING.LIB and KING.FNT (any case) in dir. */
int res_load_dir(const char *dir);
const char *res_error(void);

void res_init(SDL_Renderer *r);
int res_sprite_w(int id);
int res_sprite_h(int id);
/* draw sprite id with its top-left at (x, y); pixels of colour `transparent` are not drawn */
void res_sprite(int id, int x, int y, int transparent);
/* part of a sprite: source rectangle in the sprite */
void res_sprite_part(int id, int sx, int sy, int w, int h, int x, int y, int transparent);

/* UTF-8 text in the original's fonts (CP866), `advance` pixels per character (8 normally) */
void res_text(int font, int x, int y, int ega, int advance, const char *s);
void res_textf(int font, int x, int y, int ega, int advance, const char *fmt, ...);
int res_text_len(const char *s); /* characters */
/* text with a shadow one pixel right and down, as the game draws its headings */
void res_text_shadow(int font, int x, int y, int ega, int shadow, int advance, const char *s);

void res_fill(int x, int y, int w, int h, int ega);
void res_frame(int x, int y, int w, int h, int ega);
/* the game's raised box (draw_box): fill, light top-left, dark bottom-right */
void res_box(int x, int y, int w, int h, int ega);

#endif
