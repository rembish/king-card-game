/* KING (1993) for SDL2: the original's rules from core/, its pictures and fonts from the
 * player's own KING.LIB / KING.FNT, drawn on the original's 640x350 screen. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "audio.h"
#include "club.h"
#include "kg_core.h"
#include "net.h"
#include "res.h"
#include "store.h"

#define SCALE 3 /* the 640x350 screen is drawn 3x, then stretched to 4:3 */
#define POLL_MS 30

enum { SC_NOFILES, SC_TITLE, SC_NAME, SC_PARTNERS, SC_TABLE, SC_OVER };
enum {
    PH_DEALING,
    PH_CONTRACT_HUMAN,
    PH_CONTRACT_AI,
    PH_CONTRACT_SHOW,
    PH_TURN,
    PH_AI_THINK,
    PH_HUMAN,
    PH_FLY,
    PH_TRICK_WAIT,
    PH_TRICK_TAKE,
    PH_SUMMARY,
};
/* frontend keys beyond the core's */
enum { K_ENTER = 0x200, K_ESC, K_BACKSPACE, K_ANY };

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *screen;
static int running = 1;
static int ignore_text; /* the key that left the title page must not be typed into the name */

static kg_game G;
static club_t club;
static int member = -1, member_new;
static int partner[4]; /* cell 0..11 of the partner in seats 1..3 */
static int npartners;

static int sc = SC_TITLE, ph;
static Uint32 now, ph_t0, poll_t;
static int demo;                 /* the computer plays the human's seat too */
static const char *replay_polls; /* --replay: the polls come from here, no display */

/* keys waiting for the next poll */
static int keyq[64], nkeys;
/* every poll of the game (kg_poll_token), for a replay */
static char *polls;
static size_t npolls, polls_cap;

/* what is on the screen */
static int16_t hx[KG_HAND + 1]; /* x of each card in the human's hand */
static int16_t table_card[5];
static int dealt;              /* cards shown dealt so far */
static int deal_start;         /* G.dealer before the deal */
static int decl;
static struct {
    int card, x0, y0, x1, y1, face;
} fly;
static char name_buf[CLUB_NAME * 2 + 1];
static int pcur_col, pcur_row;
static char chron[CLUB_LINES][CLUB_LINE_LEN];
static int nchron, game_total;
static const char *msg;
static int online;      /* this game was dealt by the club's server and will be booked there */
static int name_wait;   /* asking the club whether the name is free */
static int result_wait; /* the game is being booked */
static const char *net_note;
/* the end screen's table: the club's richest, the global club's when it answered */
static struct {
    char name[CLUB_NAME * 2 + 1];
    long balance;
    unsigned games;
} rows[9];
static int nrows;

/* seats: 1 left, 2 top, 3 right, 4 the human (from ds:0004.. of KING2) */
static const int hand_x[5] = {0, 12, 296, 492, 0}, hand_y[5] = {0, 115, 45, 115, 280};
static const int tab_x[5] = {0, 242, 294, 346, 294}, tab_y[5] = {0, 150, 120, 150, 180};
static const int face_x[5] = {0, 16, 196, 544, 0}, face_y[5] = {0, 10, 2, 10, 0};
#define DECK_X 294
#define DECK_Y 145

static const char *partner_names[12] = {"Винни Пух", "Кролик", "Иа-Иа",   "Пятачок", "Фрекен Бок", "Багира",
                                        "Сова",      "Оля",    "Мишка",   "Башуров", "Карлсон",    "Борька"};
static const char *contract_rows[7] = {"     ВЗЯТКИ", "      ЧЕРВИ", "   МАЛЬЧИКИ", "    ДЕВОЧКИ",
                                       "2 ПОСЛЕДНИЕ", "       КИНГ", " ВСЕ ПОДРЯД"};
static const char *contract_names[7] = {"ВЗЯТКИ", "ЧЕРВЕЙ", "МАЛЬЧИКОВ", "ДЕВОЧЕК",
                                        "2 ПОСЛЕДНИЕ", "КИНГА", "ВСЕ ПОДРЯД"};

static void set_phase(int p)
{
    ph = p;
    ph_t0 = now;
}

static void push_key(int k)
{
    if (nkeys < 64) keyq[nkeys++] = k;
}

static int pop_key(void)
{
    if (!nkeys) return KG_KEY_NONE;
    int k = keyq[0];
    memmove(keyq, keyq + 1, (size_t)--nkeys * sizeof keyq[0]);
    return k;
}

static void log_poll(int key)
{
    if (npolls + 2 > polls_cap) {
        polls_cap = polls_cap ? polls_cap * 2 : 1 << 16;
        polls = realloc(polls, polls_cap);
        if (!polls) {
            fprintf(stderr, "king: out of memory\n");
            exit(1);
        }
    }
    polls[npolls++] = kg_poll_token(key);
    polls[npolls] = 0;
}

/* the key for one poll of the human's loops */
static int poll_key(void)
{
    if (demo) return KG_KEY_PICK(kg_ai_choose(&G, KG_HUMAN));
    if (replay_polls) {
        int k = *replay_polls ? kg_poll_key(*replay_polls++) : -1;
        if (k < 0) {
            fprintf(stderr, "king: replay polls exhausted or bad\n");
            exit(2);
        }
        return k;
    }
    int k = pop_key();
    if (k >= K_ENTER) k = k == K_ENTER ? KG_KEY_SPACE : KG_KEY_OTHER;
    return k;
}

/* Port decision: after a pause longer than a second (a hidden tab, a stalled machine) the
 * polls resume from now instead of catching up thousands at once. The log records exactly
 * the polls made, so a replay is unaffected. */
static int poll_due(void)
{
    if (replay_polls || demo) return 1;
    if (now - poll_t > 1000) poll_t = now - POLL_MS;
    if (now - poll_t < POLL_MS) return 0;
    poll_t += POLL_MS;
    return 1;
}

static int auto_continue(void) { return demo || replay_polls; }
static Uint32 demo_until; /* the demo waits this long for the club's seed */

/* ---- layout helpers ---- */

static void human_positions(void)
{
    const int16_t *h = G.hands[KG_HUMAN];
    int x = 0x90, suit = 0;
    for (int i = 1; i <= h[0]; i++) {
        if (KG_SUIT(h[i]) == suit)
            x += 20;
        else {
            x += 60;
            suit = KG_SUIT(h[i]);
        }
        hx[i] = (int16_t)x;
    }
}

static int card_at(int mx, int my)
{
    const int16_t *h = G.hands[KG_HUMAN];
    if (my < hand_y[4] || my >= hand_y[4] + 60) return 0;
    for (int i = h[0]; i >= 1; i--)
        if (mx >= hx[i] && mx < hx[i] + 52) return i;
    return 0;
}

/* ---- drawing ---- */

static void draw_face(int seat)
{
    int cell = partner[seat];
    res_sprite_part(SPR_STRIP + cell / 4, (cell % 4) * 80, 0, 80, 88, face_x[seat], face_y[seat], SPR_NONE);
    res_frame(face_x[seat] - 1, face_y[seat] - 1, 82, 90, 0);
    const char *n = partner_names[cell];
    res_text(FONT_8, face_x[seat] + 40 - res_text_len(n) * 4, face_y[seat] + 91, 15, 8, n);
}

static void draw_hands(void)
{
    for (int p = 1; p <= 3; p++) {
        int n = G.hands[p][0];
        if (ph == PH_DEALING) {
            n = 0;
            for (int i = 0; i < dealt; i++) n += (deal_start + i) % 4 + 1 == p;
        }
        for (int i = 0; i < n; i++) res_sprite(SPR_BACK, hand_x[p] + i * 12, hand_y[p], 2);
    }
    const int16_t *h = G.hands[KG_HUMAN];
    if (ph == PH_DEALING) {
        int n = 0;
        for (int i = 0; i < dealt; i++) n += (deal_start + i) % 4 + 1 == 4;
        for (int i = 0; i < n; i++) res_sprite(SPR_BACK, 166 + i * 20, hand_y[4], 2);
        return;
    }
    for (int i = 1; i <= h[0]; i++) res_sprite(h[i], hx[i], hand_y[4], 2);
    if (ph == PH_HUMAN && !demo) {
        int spr = G.blink == 0 ? SPR_HAND : (G.blink % 8 < 4 ? SPR_HAND_BLINK + 1 : SPR_HAND_BLINK);
        res_sprite(spr, hx[G.cursor] + 4, 0x142, 2);
    }
}

static void draw_panel(void)
{
    if (ph == PH_DEALING || ph == PH_CONTRACT_HUMAN || ph == PH_CONTRACT_AI) return;
    res_box(14, 256, 117, 54, 2);
    const char *l1 = G.deal_no < 7 ? "Не брать" : "Брать";
    const char *l2 = contract_names[G.deal_no % 7];
    char l3[16];
    int price = G.price < 0 ? -G.price : G.price;
    if (G.deal_no % 7 == 6) price = 960;
    snprintf(l3, sizeof l3, "%s%d$", G.deal_no < 7 ? "-" : "+", price);
    const char *l[3] = {l1, l2, l3};
    for (int k = 0; k < 3; k++)
        res_text_shadow(FONT_14, 73 - res_text_len(l[k]) * 5, 258 + k * 15, 13, 0, 10, l[k]);
}

static void draw_counter(int value, int row, int col)
{
    int x = 0x209 + col * 32, y = 0xfe + row * 24;
    res_box(x, y, 30, 23, 2);
    char b[8];
    snprintf(b, sizeof b, "%d", value);
    int tx = x + 15 - res_text_len(b) * 4, ty = y + 5;
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++) res_text(FONT_14, tx + dx, ty + dy, 0, 8, b);
    res_text(FONT_14, tx, ty, 15, 8, b);
}

static void draw_counters(void)
{
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) res_sprite(SPR_CORNER + 1, 0x20c + i * 72, 0xff + j * 55, 7);
    draw_counter(G.taken[1], 1, 0);
    draw_counter(G.taken[2], 0, 1);
    draw_counter(G.taken[3], 1, 2);
    draw_counter(G.taken[4], 2, 1);
    draw_counter(G.items_left, 1, 1);
}

static void bubble(int seat, const char *text)
{
    int x = seat == 4 ? 0x1c0 : face_x[seat] + (seat == 3 ? -92 : 84), y = seat == 4 ? 0x105 : face_y[seat] + 30;
    res_box(x, y, 88, 15, 7);
    res_text(FONT_8, x + 4, y + 4, 0, 8, text);
}

static void draw_grid(void)
{
    res_box(176, 112, 216, 120, 2);
    res_text_shadow(FONT_8, 0x11c - 64, 0x7c, 15, 0, 8, "НЕ БРАТЬ  БРАТЬ");
    for (int r = 0; r < 7; r++) {
        res_text_shadow(FONT_8, 0xb8, 0x8c + r * 14, 15, 0, 8, contract_rows[r]);
        if (!G.played[decl][r]) res_sprite(SPR_SNICKERS, 0x120, 0x8a + r * 14, 2);
        if (!G.played[decl][7 + r]) res_sprite(SPR_MARS, 0x160, 0x8a + r * 14, 2);
    }
    if (ph == PH_CONTRACT_HUMAN && !demo)
        res_sprite(SPR_HAND_BLINK, G.grid_half * 64 + 0x134, G.grid_row * 14 + 0x89, 2);
}

static void draw_table(void)
{
    res_fill(0, 0, RES_W, RES_H, 2);
    for (int p = 1; p <= 3; p++) draw_face(p);
    draw_hands();
    if (ph == PH_DEALING && dealt < KG_CARDS) res_sprite(SPR_BACK, DECK_X, DECK_Y, 2);
    for (int p = 1; p <= 4; p++)
        if (table_card[p]) res_sprite(table_card[p], tab_x[p], tab_y[p], 2);
    if (ph == PH_TRICK_TAKE && (now - ph_t0) / 120 % 2 == 0 && G.last_winner)
        res_frame(tab_x[G.last_winner] - 2, tab_y[G.last_winner] - 2, 56, 64, 14);
    if (ph == PH_FLY || ph == PH_DEALING) {
        float t = ph == PH_FLY ? (float)(now - ph_t0) / 220.f : 1.f;
        if (t > 1) t = 1;
        if (ph == PH_FLY)
            res_sprite(fly.face ? fly.card : SPR_BACK, fly.x0 + (int)((fly.x1 - fly.x0) * t),
                       fly.y0 + (int)((fly.y1 - fly.y0) * t), 2);
    }
    draw_panel();
    if (ph != PH_DEALING) draw_counters();
    if (ph == PH_CONTRACT_HUMAN || ph == PH_CONTRACT_AI) draw_grid();
    if (ph == PH_CONTRACT_AI && decl != 4) bubble(decl, "Выбираю...");
    if (ph == PH_AI_THINK) bubble(kg_turn(&G), "Думаю...");
    if (ph == PH_TRICK_WAIT) res_text_shadow(FONT_8, 0x1d0, 0x150, 14, 0, 8, "Пробел - дальше");
    if (ph == PH_SUMMARY) {
        res_box(150, 120, 340, 110, 7);
        res_text(FONT_8, 0xc0, 128, 0, 8, "             Счет:   Итого:");
        for (int p = 1; p <= 4; p++) {
            const char *n = p == 4 ? club.m[member].name : partner_names[partner[p]];
            int y = 128 + p * 18;
            res_text(FONT_8, 0xa0, y, 0, 8, n);
            res_textf(FONT_8, 0x150, y, G.deal_score[p] < 0 ? 4 : 1, 8, "%6d", G.deal_score[p]);
            res_textf(FONT_8, 0x190, y, G.total[p] + G.deal_score[p] < 0 ? 4 : 1, 8, "%6d",
                      G.total[p] + G.deal_score[p]);
        }
    }
}

static void draw_title(void)
{
    res_fill(0, 0, RES_W, RES_H, 7);
    res_sprite(SPR_LOGO_KP, 188, 6, SPR_NONE);
    res_text(FONT_6, 40, 30, 0, 6, "ОСНОВАНА");
    res_text(FONT_6, 40, 38, 0, 6, "В 1925 Г");
    static const char *months[12] = {"января", "февраля", "марта",    "апреля",  "мая",    "июня",
                                     "июля",   "августа", "сентября", "октября", "ноября", "декабря"};
    static const char *days[7] = {"воскресенье", "понедельник", "вторник", "среда",
                                  "четверг",     "пятница",     "суббота"};
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    res_fill(10, 64, 620, 1, 0);
    res_textf(FONT_6, 14, 67, 0, 6, "%s, %d %s %d г.   N 118   Цена договорная", days[tm->tm_wday], tm->tm_mday,
              months[tm->tm_mon], tm->tm_year + 1900);
    res_fill(10, 76, 620, 1, 0);
    static const char *lines[] = {"\"Комсомольская правда\"", "дарит", "своим дорогим читателям",
                                  "старую добрую карточную игру", "под названием"};
    for (int i = 0; i < 5; i++)
        res_text_shadow(FONT_14, 320 - res_text_len(lines[i]) * 4, 88 + i * 15, 0, 15, 8, lines[i]);
    res_sprite(SPR_LOGO_KING, 222, 166, 7);
    res_text(FONT_8, 320 - 19 * 4, 258, 0, 8, "Полный вариант игры");
    res_text(FONT_8, 320 - 29 * 4, 268, 0, 8, "Версия 1.1 от 22 авг. 1993 г.");
    res_text(FONT_8, 320 - 31 * 4, 282, 0, 8, "(C) Дима Башуров из Арзамаса-16");
    res_text(FONT_6, 320 - 37 * 3, 300, 8, 6, "Этот порт: правила по KING2.EXE, 2026");
    if (now / 500 % 2) res_text(FONT_8, 320 - 23 * 4, 326, 4, 8, "Нажмите любую клавишу...");
}

static void draw_name(void)
{
    res_fill(0, 0, RES_W, RES_H, 2);
    res_sprite(SPR_LOGO_KING, 222, 20, 2);
    res_box(40, 140, 560, 70, 7);
    res_text(FONT_8, 60, 154, 0, 8, "Добро пожаловать! Как Ваше имя? ( Не более 12 символов)");
    res_box(220, 176, 200, 20, 15);
    res_text(FONT_14, 228, 179, 0, 8, name_buf);
    if (now / 300 % 2) res_fill(228 + res_text_len(name_buf) * 8, 190, 8, 2, 0);
    if (club.n) {
        res_text(FONT_8, 60, 228, 15, 8, "Члены клуба:");
        int top[9], n = club_top(&club, top);
        for (int i = 0; i < n; i++) res_text(FONT_8, 60 + (i % 3) * 180, 242 + (i / 3) * 12, 14, 8, club.m[top[i]].name);
    }
    if (name_wait) res_text(FONT_8, 320 - 16 * 4, 330, 14, 8, "Спрашиваю клуб...");
    else if (msg) res_text(FONT_8, 320 - res_text_len(msg) * 4, 330, 14, 8, msg);
}

static int picked(int cell)
{
    for (int s = 1; s <= npartners; s++)
        if (partner[s] == cell) return s;
    return 0;
}

static void draw_partners(void)
{
    res_fill(0, 0, RES_W, RES_H, 2);
    static const char *who[3][2] = {{"ОН ИГРАЕТ", "НЕПЛОХО"}, {"ОНА ИГРАЕТ", "ОТЛИЧНО"}, {"ОН ВСЕГДА", "МУХЛЮЕТ"}};
    for (int row = 0; row < 3; row++) {
        res_sprite(SPR_STRIP + row, 24, 11 + row * 104, SPR_NONE);
        for (int col = 0; col < 4; col++) {
            int cell = row * 4 + col, x = 24 + col * 80, y = 11 + row * 104;
            res_text(FONT_8, x + 40 - res_text_len(partner_names[cell]) * 4, y + 91, 15, 8, partner_names[cell]);
            int s = picked(cell);
            if (s) {
                res_box(x + 1, y + 10, 78, 64, 7);
                res_textf(FONT_8, x + 26, y + 16, 14, 8, "%d-й", s);
                res_text(FONT_8, x + 12, y + 28, 14, 8, "ПАРТНЕР");
                res_text(FONT_8, x + 40 - res_text_len(who[row][0]) * 4, y + 44, 14, 8, who[row][0]);
                res_text(FONT_8, x + 40 - res_text_len(who[row][1]) * 4, y + 56, 14, 8, who[row][1]);
            }
        }
    }
    if (now / 250 % 2) res_frame(24 + pcur_col * 80, 11 + pcur_row * 104, 80, 104, 14);
    res_box(360, 120, 270, 60, 7);
    res_text(FONT_8, 368, 130, 0, 8, "Рад поиграть с Вами!");
    res_textf(FONT_8, 368, 146, 0, 8, "Выберите себе %d-го партнера...", npartners + 1);
    res_text(FONT_6, 368, 164, 8, 6, "стрелки и пробел, или мышь");
}

static void draw_over(void)
{
    res_fill(0, 0, RES_W, RES_H, 2);
    res_box(0x88, 4, 0x1b8 - 0x88, 342, 7);
    int y = 12;
    for (int i = 0; i < nchron; i++, y += 9) res_text(FONT_8, 0x98, y, 0, 8, chron[i]);
    y += 6;
    res_text(FONT_8, 0x98, y, 0, 8, "НАШИ МИЛЛИОНЕРЫ.(Из банк.счетов)");
    y += 12;
    res_text(FONT_8, 0x98, y, 0, 8, "   член        лицевой    число");
    res_text(FONT_8, 0x98, y + 9, 0, 8, "   клуба         счет      игр");
    y += 20;
    for (int i = 0; i < nrows; i++, y += 9) {
        if (rows[i].balance < 0) continue; /* the original lists no debtors */
        int me = !strcmp(rows[i].name, club.m[member].name);
        res_textf(FONT_8, 0x98, y, me ? 4 : 0, 8, "%d %s", i + 1, rows[i].name);
        res_textf(FONT_8, 0x110 - 8, y, 0, 8, "$%7ld", rows[i].balance);
        res_textf(FONT_8, 0x168, y, 0, 8, "%4u", rows[i].games);
    }
    if (result_wait) res_text(FONT_6, 0x98, 334, 8, 6, "Сообщаю в клуб...");
    else if (net_note) res_text(FONT_6, 0x98, 334, 4, 6, net_note);
    res_sprite(SPR_BILL, 20, 140, 2);
    res_sprite(SPR_BILL, 544, 140, 2);
}

static void draw_nofiles(void)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
}

/* ---- game flow ---- */

static void start_deal(void)
{
    deal_start = G.dealer;
    kg_deal(&G);
    human_positions();
    memset(table_card, 0, sizeof table_card);
    dealt = 0;
    res_set_palette(13, 0x3b);
    set_phase(PH_DEALING);
}

static void after_deal(void)
{
    decl = kg_declarer(&G);
    nkeys = 0;
    if (decl == KG_HUMAN && !demo) {
        kg_contract_begin(&G);
        poll_t = now;
        set_phase(PH_CONTRACT_HUMAN);
    } else {
        if (decl == KG_HUMAN) {
            kg_contract_begin(&G);
            int k;
            do k = KG_KEY_PICK(rand() % KG_GAMES);
            while (G.played[KG_HUMAN][k - KG_KEY_PICK(0)]);
            log_poll(k);
            kg_contract_poll(&G, k);
        } else
            kg_ai_contract(&G);
        set_phase(PH_CONTRACT_AI);
    }
}

static void contract_chosen(void)
{
    res_set_palette(13, G.deal_no < 7 ? 0x3b : 0x3c);
    audio_beep(120, 50);
    kg_start_play(&G);
    set_phase(PH_CONTRACT_SHOW);
}

static void start_fly(int p, int idx_x, int card)
{
    fly.card = card;
    fly.face = 1;
    fly.x0 = p == 4 ? idx_x : hand_x[p] + idx_x;
    fly.y0 = hand_y[p];
    fly.x1 = tab_x[p];
    fly.y1 = tab_y[p];
    set_phase(PH_FLY);
}

static void next_turn(void)
{
    if (kg_deal_done(&G)) {
        nkeys = 0; /* deal_summary waits with wait_space_or_click, which flushes */
        set_phase(PH_SUMMARY);
        return;
    }
    int p = kg_turn(&G);
    if (p == KG_HUMAN) { /* in the demo the computer plays it, through the same polls */
        kg_human_begin(&G);
        nkeys = 0; /* flush_input */
        poll_t = now;
        set_phase(PH_HUMAN);
    } else
        set_phase(PH_AI_THINK);
}

static void local_rows(void)
{
    int top[9];
    nrows = club_top(&club, top);
    for (int i = 0; i < nrows; i++) {
        snprintf(rows[i].name, sizeof rows[i].name, "%s", club.m[top[i]].name);
        rows[i].balance = club.m[top[i]].balance;
        rows[i].games = club.m[top[i]].games;
    }
}

static void game_over(void)
{
    game_total = G.total[KG_HUMAN];
    club_finish(&club, member, game_total);
    const club_member *m = &club.m[member];
    nchron = club_chronicle(m->name, game_total, m->balance, club.n, member_new, chron);
    local_rows();
    net_note = NULL;
    result_wait = 0;
    if (online && !replay_polls) {
        net_submit(m->name, polls ? polls : "");
        result_wait = 1;
    }
    sc = SC_OVER;
}

/* the club's answer: its numbers replace this browser's */
static void result_update(void)
{
    static char buf[4096];
    int r = net_result(buf, sizeof buf);
    if (r == NET_PENDING) return;
    result_wait = 0;
    if (r != NET_OK) {
        net_note = r == NET_TAKEN ? "Клуб: это имя занято другим членом клуба." : "Клуб не ответил: игра записана только здесь.";
        return;
    }
    int members = 0, total = 0, games = 0, is_new = 0;
    long balance = 0;
    char *line = strtok(buf, "\n");
    if (!line || sscanf(line, "%d %d %ld %d %d", &members, &total, &balance, &games, &is_new) != 5) return;
    nrows = 0;
    while ((line = strtok(NULL, "\n")) && nrows < 9) {
        int used = 0;
        if (sscanf(line, "%ld %u %n", &rows[nrows].balance, &rows[nrows].games, &used) < 2) continue;
        snprintf(rows[nrows].name, sizeof rows[nrows].name, "%s", line + used);
        nrows++;
    }
    nchron = club_chronicle(club.m[member].name, total, balance, members, is_new, chron);
}

static void table_update(void)
{
    switch (ph) {
    case PH_DEALING:
        dealt = auto_continue() ? KG_CARDS : (int)((now - ph_t0) / 35);
        if (dealt >= KG_CARDS) {
            dealt = KG_CARDS;
            after_deal();
        }
        break;
    case PH_CONTRACT_HUMAN:
        while (poll_due()) {
            int k = poll_key();
            log_poll(k);
            if (kg_contract_poll(&G, k)) {
                contract_chosen();
                break;
            }
            if (k == KG_KEY_SPACE || k >= KG_KEY_PICK(0)) audio_beep(1000, 100);
        }
        break;
    case PH_CONTRACT_AI:
        if (now - ph_t0 > (auto_continue() ? 0u : 1500u) || pop_key()) contract_chosen();
        break;
    case PH_CONTRACT_SHOW:
        if (now - ph_t0 > (auto_continue() ? 0u : 700u)) next_turn();
        break;
    case PH_AI_THINK:
        if (now - ph_t0 > (auto_continue() ? 0u : 380u)) {
            int p = kg_turn(&G), i = kg_ai_choose(&G, p);
            int x = p == 4 ? hx[i] : (i - 1) * 12;
            int card = G.hands[p][i];
            if (p == 4) memmove(&hx[i], &hx[i + 1], (size_t)(KG_HAND - i) * sizeof hx[0]);
            kg_play(&G, p, i);
            start_fly(p, x, card);
        }
        break;
    case PH_HUMAN:
        while (poll_due()) {
            int k = poll_key();
            log_poll(k);
            int card_i = k >= KG_KEY_PICK(1) ? k - KG_KEY_PICK(0) : G.cursor;
            int16_t x = hx[card_i];
            if (kg_human_poll(&G, k)) {
                int i = G.cursor;
                memmove(&hx[i], &hx[i + 1], (size_t)(KG_HAND - i) * sizeof hx[0]);
                audio_beep(100, 30);
                start_fly(4, x, G.hands[4][9]);
                break;
            }
            if (k == KG_KEY_SPACE || k >= KG_KEY_PICK(1)) audio_beep(1000, 100);
        }
        break;
    case PH_FLY:
        if (now - ph_t0 >= (auto_continue() ? 0u : 220u)) {
            int p = G.current;
            table_card[p] = G.hands[p][9];
            kg_after_card(&G);
            if (G.in_trick == 0) {
                nkeys = 0;
                set_phase(PH_TRICK_WAIT);
            } else
                next_turn();
        }
        break;
    case PH_TRICK_WAIT:
        if (pop_key() || auto_continue()) {
            for (int i = 1; i <= 20; i++) { /* finish_trick */
                audio_beep(i * 20, 2);
                audio_beep(0, i / 2 + 1);
            }
            set_phase(PH_TRICK_TAKE);
        }
        break;
    case PH_TRICK_TAKE:
        if (now - ph_t0 > (auto_continue() ? 0u : 700u)) {
            memset(table_card, 0, sizeof table_card);
            next_turn();
        }
        break;
    case PH_SUMMARY:
        if (pop_key() || auto_continue()) {
            kg_end_deal(&G);
            if (kg_game_over(&G))
                game_over();
            else
                start_deal();
        }
        break;
    }
}

static void start_game(uint32_t seed)
{
    kg_new_game(&G, seed);
    npolls = 0;
    sc = SC_TABLE;
    start_deal();
}

/* ---- input ---- */

static void to_logical(int wx, int wy, int *x, int *y)
{
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    int dw = w, dh = w * 3 / 4;
    if (dh > h) {
        dh = h;
        dw = h * 4 / 3;
    }
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);
    float k = (float)w / (float)ww; /* high-DPI */
    float px = wx * k - (w - dw) / 2.f, py = wy * k - (h - dh) / 2.f;
    *x = (int)(px * RES_W / dw);
    *y = (int)(py * RES_H / dh);
}

static void click(int x, int y)
{
    switch (sc) {
    case SC_TITLE: sc = SC_NAME; break;
    case SC_PARTNERS:
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 4; col++)
                if (x >= 24 + col * 80 && x < 104 + col * 80 && y >= 11 + row * 104 && y < 115 + row * 104) {
                    pcur_col = col;
                    pcur_row = row;
                    push_key(KG_KEY_SPACE);
                }
        break;
    case SC_TABLE:
        if (ph == PH_HUMAN) {
            int i = card_at(x, y);
            if (i) push_key(KG_KEY_PICK(i));
        } else if (ph == PH_CONTRACT_HUMAN) {
            int row = (y - 0x8a) / 14, half = x >= 0x150 ? 1 : 0;
            if (y >= 0x8a && row < 7 && x >= 0x110 && x < 0x190) push_key(KG_KEY_PICK(half * 7 + row));
        } else
            push_key(K_ANY);
        break;
    case SC_OVER: sc = SC_TITLE; break;
    }
}

static void key(SDL_Keysym ks)
{
    int k = 0;
    switch (ks.sym) {
    case SDLK_SPACE: k = KG_KEY_SPACE; break;
    case SDLK_LEFT: case SDLK_KP_4: k = KG_KEY_LEFT; break;
    case SDLK_RIGHT: case SDLK_KP_6: k = KG_KEY_RIGHT; break;
    case SDLK_UP: case SDLK_KP_8: k = KG_KEY_UP; break;
    case SDLK_DOWN: case SDLK_KP_2: k = KG_KEY_DOWN; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: k = K_ENTER; break;
    case SDLK_ESCAPE: k = K_ESC; break;
    case SDLK_BACKSPACE: k = K_BACKSPACE; break;
    default: k = K_ANY;
    }
    if (ks.sym == SDLK_F11 || (ks.sym == SDLK_RETURN && (ks.mod & KMOD_ALT))) {
        Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    switch (sc) {
    case SC_TITLE:
        sc = SC_NAME;
        ignore_text = 1;
        SDL_StartTextInput();
        break;
    case SC_NAME:
        if (k == K_BACKSPACE) {
            size_t n = strlen(name_buf);
            while (n && (name_buf[n - 1] & 0xc0) == 0x80) n--;
            if (n) name_buf[n - 1] = 0;
        } else if (k == K_ENTER && name_buf[0] && !name_wait) {
            msg = NULL;
            net_claim(name_buf);
            name_wait = 1;
        } else if (k == K_ESC)
            sc = SC_TITLE;
        break;
    case SC_PARTNERS:
        if (k == KG_KEY_LEFT) pcur_col = (pcur_col + 3) % 4;
        if (k == KG_KEY_RIGHT) pcur_col = (pcur_col + 1) % 4;
        if (k == KG_KEY_UP) pcur_row = (pcur_row + 2) % 3;
        if (k == KG_KEY_DOWN) pcur_row = (pcur_row + 1) % 3;
        if (k == KG_KEY_SPACE || k == K_ENTER) push_key(KG_KEY_SPACE);
        break;
    case SC_TABLE:
        if (k == K_ESC) {
            sc = SC_TITLE;
            return;
        }
        push_key(k);
        break;
    case SC_OVER: sc = SC_TITLE; break;
    }
}

static void text_input(const char *t)
{
    if (sc != SC_NAME) return;
    for (const char *s = t; *s;) {
        int len = (*s & 0x80) == 0 ? 1 : (*s & 0xe0) == 0xc0 ? 2 : 3;
        unsigned c0 = (unsigned char)s[0], c1 = (unsigned char)s[1];
        int ok = (c0 >= 0x20 && c0 < 0x7f) || (c0 == 0xd0 && (c1 >= 0x81 && c1 <= 0xbf)) ||
                 (c0 == 0xd1 && (c1 >= 0x80 && c1 <= 0x91));
        size_t used = strlen(name_buf);
        if (ok && res_text_len(name_buf) < CLUB_NAME && used + (size_t)len < sizeof name_buf) {
            memcpy(name_buf + used, s, (size_t)len);
            name_buf[used + (size_t)len] = 0;
        }
        s += len;
    }
}

/* the club said whether the name is free (natively there is no club: always free) */
static void name_update(void)
{
    int r = net_claim_status();
    if (r == NET_PENDING) return;
    name_wait = 0;
    if (r == NET_TAKEN) {
        msg = "Это имя в клубе уже занято. Выберите другое.";
        return;
    }
    SDL_StopTextInput();
    member = club_join(&club, name_buf, &member_new);
    club_save(&club);
    npartners = 0;
    net_new_game();
    sc = SC_PARTNERS;
}

static void partners_update(void)
{
    int k = pop_key();
    if (k != KG_KEY_SPACE) return;
    int cell = pcur_row * 4 + pcur_col;
    if (picked(cell)) {
        audio_beep(1000, 100);
        return;
    }
    partner[++npartners] = cell;
    for (int i = 1; i <= 10; i++) { /* choose_partners */
        audio_beep(500 - 10 * i, i / 5 + 2);
        audio_beep(0, 2);
    }
    if (npartners == 3) {
        uint32_t seed;
        online = net_game_seed(&seed);
        if (!online) seed = (uint32_t)time(NULL) ^ (uint32_t)SDL_GetPerformanceCounter();
        start_game(seed);
    }
}

/* ---- frame ---- */

static void present(void)
{
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    int dw = w, dh = w * 3 / 4;
    if (dh > h) {
        dh = h;
        dw = h * 4 / 3;
    }
    SDL_Rect d = {(w - dw) / 2, (h - dh) / 2, dw, dh};
    SDL_RenderCopy(ren, screen, NULL, &d);
    SDL_RenderPresent(ren);
}

static void render(void)
{
    SDL_SetRenderTarget(ren, screen);
    SDL_RenderSetScale(ren, SCALE, SCALE);
    switch (sc) {
    case SC_NOFILES: draw_nofiles(); break;
    case SC_TITLE: draw_title(); break;
    case SC_NAME: draw_name(); break;
    case SC_PARTNERS: draw_partners(); break;
    case SC_TABLE: draw_table(); break;
    case SC_OVER: draw_over(); break;
    }
    SDL_RenderSetScale(ren, 1, 1);
}

static void frame(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = 0;
        if (e.type == SDL_KEYDOWN) {
            audio_resume();
            key(e.key.keysym);
        }
        if (e.type == SDL_TEXTINPUT && !ignore_text) text_input(e.text.text);
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            audio_resume();
            int x, y;
            to_logical(e.button.x, e.button.y, &x, &y);
            click(x, y);
        }
    }
    ignore_text = 0;
    now = SDL_GetTicks();
    if (demo_until && sc == SC_TITLE) {
        uint32_t seed;
        if ((online = net_game_seed(&seed)) || now > demo_until) {
            demo_until = 0;
            start_game(online ? seed : (uint32_t)time(NULL));
        }
    }
    if (sc == SC_NAME && name_wait) name_update();
    if (sc == SC_OVER && result_wait) result_update();
    if (sc == SC_PARTNERS) partners_update();
    if (sc == SC_TABLE) table_update();
    if (sc == SC_NAME && !SDL_IsTextInputActive()) SDL_StartTextInput();
    render();
    present();
#ifdef __EMSCRIPTEN__
    if (!running) emscripten_cancel_main_loop();
#endif
}

/* --shot FILE SCREEN: render one screen to a BMP (for checking the layout without a display) */
static int shot(const char *file, const char *what)
{
    now = 1000;
    member = club_join(&club, "Алекс", &member_new);
    partner[1] = 0;
    partner[2] = 5;
    partner[3] = 10;
    npartners = 3;
    if (!strcmp(what, "title")) sc = SC_TITLE;
    if (!strcmp(what, "name")) {
        sc = SC_NAME;
        snprintf(name_buf, sizeof name_buf, "Алекс");
    }
    if (!strcmp(what, "partners")) {
        sc = SC_PARTNERS;
        npartners = 2;
    }
    if (!strncmp(what, "table", 5) || !strcmp(what, "over")) {
        kg_new_game(&G, 7);
        sc = SC_TABLE;
        demo = 1;
        start_deal();
        int steps = atoi(what + 5 + (what[5] == ':'));
        for (int i = 0; i < steps * 40; i++) {
            now += 50;
            table_update();
            if (sc != SC_TABLE) break;
        }
        demo = 0;
        if (!strcmp(what, "over")) {
            while (sc == SC_TABLE) {
                demo = 1;
                now += 400;
                table_update();
            }
        }
    }
    render();
    SDL_SetRenderTarget(ren, screen);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, RES_W * SCALE, RES_H * SCALE, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    int r = SDL_SaveBMP(s, file);
    SDL_FreeSurface(s);
    return r;
}

/* --replay SEED POLLS: play a whole game headless through the frontend's own paths, the
 * human's keys taken from POLLS; prints the totals and the polls made (tests/frontend.py) */
static int replay(uint32_t seed, const char *p)
{
    replay_polls = p;
    member = club_join(&club, "Replay", &member_new);
    partner[1] = 0;
    partner[2] = 5;
    partner[3] = 10;
    npartners = 3;
    now = 1;
    start_game(seed);
    while (sc == SC_TABLE) {
        now += 10;
        table_update();
    }
    printf("%d %d %d %d\n%s\n", G.total[1], G.total[2], G.total[3], G.total[4], polls ? polls : "");
    if (*replay_polls) {
        fprintf(stderr, "king: %zu replay polls left over\n", strlen(replay_polls));
        return 1;
    }
    return 0;
}

static int find_files(int argc, char **argv)
{
    const char *dirs[8];
    int n = 0;
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && (i == 1 || argv[i - 1][0] != '-')) dirs[n++] = argv[i];
    dirs[n++] = "original";
    dirs[n++] = ".";
    char *base = SDL_GetBasePath();
    if (base) dirs[n++] = base;
    char *pref = SDL_GetPrefPath("King", "King");
    if (pref) dirs[n++] = pref;
    int ok = 0;
    for (int i = 0; i < n && !ok; i++) ok = res_load_dir(dirs[i]) == 0;
    if (!ok)
        fprintf(stderr, "king: KING.LIB and KING.FNT from your copy of the game are needed. Put them in\n"
                        "  %s\nor pass their folder on the command line.\n",
                pref ? pref : ".");
    SDL_free(base);
    SDL_free(pref);
    return ok;
}

int main(int argc, char **argv)
{
    const char *shot_file = NULL, *shot_what = "title";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) {
            shot_file = argv[i + 1];
            shot_what = argv[i + 2];
        }
        if (!strcmp(argv[i], "--demo")) demo = 1;
        if (!strcmp(argv[i], "--replay") && i + 2 < argc) {
            club_readonly = 1;
            store_init();
            club_load(&club);
            return replay((uint32_t)strtoul(argv[i + 1], 0, 0), argv[i + 2]);
        }
    }
    if (shot_file) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        club_readonly = 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | (shot_file ? 0 : SDL_INIT_AUDIO)) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    store_init();
    club_load(&club);
    if (!find_files(argc, argv)) return 1;
    win = SDL_CreateWindow("KING", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (shot_file ? SDL_WINDOW_HIDDEN : 0));
    ren = SDL_CreateRenderer(win, -1, shot_file ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    res_init(ren);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    screen = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, RES_W * SCALE, RES_H * SCALE);
    if (shot_file) return shot(shot_file, shot_what) == 0 ? 0 : 1;
    audio_init();
    srand((unsigned)time(NULL));
    if (demo) {
        member = club_join(&club, "Демо", &member_new);
        partner[1] = 0;
        partner[2] = 5;
        partner[3] = 10;
        npartners = 3;
        net_new_game();
        demo_until = SDL_GetTicks() + 3000;
        sc = SC_TITLE;
    }
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (running) frame();
#endif
    SDL_Quit();
    return 0;
}
