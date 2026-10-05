#include "club.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

#define MAGIC "KGC1"

int club_readonly;

void club_load(club_t *c)
{
    static unsigned char buf[4 + 2 + CLUB_MAX * 32];
    c->n = 0;
    int n = store_read("club.bin", buf, sizeof buf);
    if (n < 6 || memcmp(buf, MAGIC, 4) != 0) return;
    int count = buf[4] | buf[5] << 8;
    const unsigned char *p = buf + 6;
    for (int i = 0; i < count && i < CLUB_MAX && p + 32 <= buf + n; i++, p += 32) {
        club_member *m = &c->m[c->n++];
        memcpy(m->name, p, 25);
        m->name[24] = 0;
        m->balance = (int32_t)((uint32_t)p[25] | (uint32_t)p[26] << 8 | (uint32_t)p[27] << 16 |
                               (uint32_t)p[28] << 24);
        m->games = (uint16_t)(p[29] | p[30] << 8);
    }
}

int club_save(const club_t *c)
{
    if (club_readonly) return 0;
    static unsigned char buf[4 + 2 + CLUB_MAX * 32];
    memcpy(buf, MAGIC, 4);
    buf[4] = (unsigned char)c->n;
    buf[5] = (unsigned char)(c->n >> 8);
    unsigned char *p = buf + 6;
    for (int i = 0; i < c->n; i++, p += 32) {
        const club_member *m = &c->m[i];
        memset(p, 0, 32);
        memcpy(p, m->name, strlen(m->name));
        uint32_t b = (uint32_t)m->balance;
        p[25] = (unsigned char)b;
        p[26] = (unsigned char)(b >> 8);
        p[27] = (unsigned char)(b >> 16);
        p[28] = (unsigned char)(b >> 24);
        p[29] = (unsigned char)m->games;
        p[30] = (unsigned char)(m->games >> 8);
    }
    return store_write("club.bin", buf, (int)(p - buf));
}

/* upper case for ASCII and Cyrillic UTF-8, the original compared with UpCase */
static void fold(char *out, const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned char *o = (unsigned char *)out;
    while (*u) {
        if (*u >= 'a' && *u <= 'z')
            *o++ = (unsigned char)(*u++ - 32);
        else if (*u == 0xd0 && u[1] >= 0xb0 && u[1] <= 0xbf) { /* а..п */
            *o++ = 0xd0;
            *o++ = (unsigned char)(u[1] - 0x20);
            u += 2;
        } else if (*u == 0xd1 && u[1] >= 0x80 && u[1] <= 0x8f) { /* р..я */
            *o++ = 0xd0;
            *o++ = (unsigned char)(u[1] + 0x20);
            u += 2;
        } else if (*u == 0xd1 && u[1] == 0x91) { /* ё */
            *o++ = 0xd0;
            *o++ = 0x81;
            u += 2;
        } else
            *o++ = *u++;
    }
    *o = 0;
}

int club_find(const club_t *c, const char *name)
{
    char a[64], b[64];
    fold(a, name);
    for (int i = 0; i < c->n; i++) {
        fold(b, c->m[i].name);
        if (!strcmp(a, b)) return i;
    }
    return -1;
}

int club_join(club_t *c, const char *name, int *is_new)
{
    int i = club_find(c, name);
    *is_new = i < 0;
    if (i >= 0) return i;
    if (c->n >= CLUB_MAX) { /* drop the poorest */
        int w = 0;
        for (int k = 1; k < c->n; k++)
            if (c->m[k].balance < c->m[w].balance) w = k;
        c->m[w] = c->m[--c->n];
    }
    i = c->n++;
    memset(&c->m[i], 0, sizeof c->m[i]);
    snprintf(c->m[i].name, sizeof c->m[i].name, "%s", name);
    return i;
}

void club_finish(club_t *c, int idx, int total)
{
    c->m[idx].balance += total;
    if (c->m[idx].games < 0xffff) c->m[idx].games++;
    club_save(c);
}

static int line(char lines[][CLUB_LINE_LEN], int n, const char *s)
{
    if (n < CLUB_LINES) snprintf(lines[n], CLUB_LINE_LEN, "%s", s);
    return n + 1;
}

int club_chronicle(const char *name, int total, long bal, int members, int is_new,
                   char lines[][CLUB_LINE_LEN])
{
    char b[CLUB_LINE_LEN];
    int n = 0;
    n = line(lines, n, "СВЕТСКАЯ ХРОНИКА.(Наш спец.корр)");
    n = line(lines, n, "Сегодня в KING- клубе состоялись");
    n = line(lines, n, "очередные игры.  Подавали свежее");
    n = line(lines, n, "пиво с воблой.На вечере были все");
    snprintf(b, sizeof b, "члены клуба числом %d.", members);
    n = line(lines, n, b);
    if (is_new) {
        n = line(lines, n, "Прошел прием новых членов клуба.");
        n = line(lines, n, "Очередным членом KING-клуба стал");
        snprintf(b, sizeof b, "товарищ %s!", name);
        n = line(lines, n, b);
    }
    { /* right-aligned to 32 characters */
        char t[CLUB_LINE_LEN / 2];
        snprintf(t, sizeof t, "Т. %s сыграл партию.", name);
        int len = 0;
        for (const unsigned char *u = (const unsigned char *)t; *u; u++) len += (*u & 0xc0) != 0x80;
        int pad = len < 32 ? 32 - len : 0;
        snprintf(b, sizeof b, "%*s%s", pad, "", t);
        n = line(lines, n, b);
    }
    if (total < 0) {
        n = line(lines, n, "Он играл как всегда безобразно и");
        snprintf(b, sizeof b, "проиграл %d долларов. Мда..", -total);
    } else {
        n = line(lines, n, " Он играл как всегда блестящеи и");
        snprintf(b, sizeof b, "выиграл %d долларов! Браво!", total);
    }
    n = line(lines, n, b);
    if (bal < 0) {
        n = line(lines, n, "     Теперь его долги составляют");
        snprintf(b, sizeof b, "%ld долларов! Ой-ой!", -bal);
    } else {
        n = line(lines, n, "     Теперь на его лицевом счету");
        snprintf(b, sizeof b, "%ld долларов! Ура!", bal);
    }
    n = line(lines, n, b);
    n = line(lines, n, "    В конце вечера атмосфера игр");
    n = line(lines, n, "была омрачена грязной  дракой, в");
    n = line(lines, n, "которой принял активное  участие");
    snprintf(b, sizeof b, "т. %s", name);
    n = line(lines, n, b);
    n = line(lines, n, "Пора прекратить эти безобразия!");
    return n < CLUB_LINES ? n : CLUB_LINES;
}

int club_top(const club_t *c, int out[9])
{
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        if (c->m[i].balance < 0) continue;
        int k = n < 9 ? n++ : 9;
        while (k > 0 && c->m[out[k - 1]].balance < c->m[i].balance) {
            if (k < 9) out[k] = out[k - 1];
            k--;
        }
        if (k < 9) out[k] = i;
    }
    return n;
}
