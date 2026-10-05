/* The KING club: members, their balance ("лицевой счёт") and games played, kept in this
 * browser / user folder. The original asked for a password; the port does not. */
#ifndef CLUB_H
#define CLUB_H

#include <stdint.h>

#define CLUB_MAX      500
#define CLUB_NAME     12 /* characters, as the original */
#define CLUB_LINES    40
#define CLUB_LINE_LEN 160

typedef struct {
    char name[CLUB_NAME * 2 + 1]; /* UTF-8 */
    int32_t balance;
    uint16_t games;
} club_member;

typedef struct {
    int n;
    club_member m[CLUB_MAX];
} club_t;

extern int club_readonly; /* never write (screenshots, tests) */
void club_load(club_t *c);
int club_save(const club_t *c);
int club_find(const club_t *c, const char *name);
/* member index for the name, adding a new member if needed (*is_new set) */
int club_join(club_t *c, const char *name, int *is_new);
/* after a game: total of the human's points */
void club_finish(club_t *c, int idx, int total);

/* the society column, as game_over (KING2 456e) writes it */
int club_chronicle(const char *name, int total, long balance, int members, int is_new,
                   char lines[][CLUB_LINE_LEN]);
/* up to 9 members with a non-negative balance, richest first */
int club_top(const club_t *c, int out[9]);

#endif
