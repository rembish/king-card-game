# KING.EXE / KING2.EXE reverse-engineering notes

The port follows **KING2.EXE** (full variant). Most of the engine is shared, so the sections below
describe KING.EXE addresses unless they say otherwise; KING2 differences are at the end. The copy
protection (Komsomolka subscription index at login) is left out of the port; it draws no random
numbers.

Addresses are Ghidra `segment:offset` with the image loaded at segment `1000`. Symbol names live
in `ghidra/names_king.txt` (KING2 names to follow). Items marked **[verify]** still need a check
(emulator or DOSBox-X).

## The original

"Кинг" by Дмитрий (В.) Башуров ("Bady"), Arzamas-16 (RFNC-VNIIEF), August 1993, distributed
with *Комсомольская правда* (the title page reads "«Комсомольская правда» дарит своим дорогим
читателям старую добрую карточную игру под названием KING").

```
f8138fec4fa888f7f0c875660e55fb2c44d7263f50fd6718690dba02649d091e  KING.EXE   v1.7 "Короткий вариант"
a86179169d78e7976e15f0ed64e80187dde40d4a33d176c699e3301e5c7ae19e  KING2.EXE  v1.1 "Полный вариант"
627e4fe496a985d8c6cc43bba5a89e7b6947093cff9bf3f77effe89723054dbe  KING.LIB   sprites
c97a2a69ebe88577d7453408bf92a1739ed6d7b5a8c010f1d3078b5b9c9dd364  KING.FNT   fonts
abf724f9d819115a89170802a9c3b9bf31e0159dc8cfb7df918db7acbfb07c09  KING.HLP   = KING2.HLP
```

`KING.OVL` is not code: it is the player registry the game writes (see below). The copy in
`original/` is a 1997 snapshot of someone's club; the port starts with an empty one.

## Binary layout

- Turbo Pascal, not packed, no overlays. Units, one code segment each:
  `1000` main program, `17a6` Graph (EGAVGA 2.00 BGI driver linked in), `1aca` Dos (`Intr`,
  `GetDate`), `1ad8` Crt, `1b3a` System. DGROUP = `1c63` (load-relative `0c63`).
  KING2: `1871` Graph, `1ba3` Crt, `1c05` System, DGROUP `1d2e`; main program is 3.4 KB larger.
- Compiler version: `Random(N)` reduces with `div` (high word mod N), which is TP 5.x/6.0, not
  7.0. **[verify]** exact version from the System init / heap variables.
- Video: `InitGraph(EGA, EGAHi)`, 640x350x16, two pages (`a000:0000` shown, `a000:7d00` back
  buffer); most drawing is direct EGA write-mode-2 / latch copies, Graph is used only for bars,
  lines and rectangles.
- Ghidra shows Pascal calls with the arguments in **reverse** source order (Pascal pushes left
  to right): `Bar(0x14f, 0x27f, 0, 0)` in the dump is `Bar(0, 0, 639, 335)`, `SetPalette(0x14, 4)`
  is `SetPalette(4, $14)`. The same holds for the game's own procedures; a harness that calls them
  directly must push in source order (first argument first) and the callee pops.
- Mouse via `Intr($33)` with registers at `ds:08de` (`ds:051e` = mouse present).

## Random numbers

- `Randomize` (`1b3a:0a12`) is called once at start: `RandSeed` (`ds:0418`) = DOS time
  (`int 21h/2Ch`, CX:DX).
- `NextRand` (`1b3a:09da`): `RandSeed = RandSeed * 0x08088405 + 1` (32-bit).
- `Random(N)` (`1b3a:098b`): `NextRand`, then `(RandSeed >> 16) mod N` (0 if N = 0).
- Every `Random` call in the program, so the port draws the same stream:
  - `deal` (`5c50`): 1000 x (`Random(32)`, `Random(32)`) swaps.
  - `human_choose_card` (`1157`): initial cursor `Random(n)` (repeated until it is on the led suit
    if the human can follow), and a cosmetic `Random(20) == 19` blink every frame while waiting.
  - `money_rain` (`27f2`): `Random(40)` / `Random(2)` per falling note (title, login, end).
  - `title_screen` (`294d`): `Random(3)` x 2 per column of the torn-paper edge (335 columns).
  - `game_over` (`450e`): `Random(70)`, `Random(25)`, `Random(2)`, `Random(50)` for the coins.
  The human's cursor and the animations consume random numbers, so the deck order depends on
  input timing; a replay must record the seed at each `deal`. **[verify]** in the emulator.

## Cards

- `card = suit * 13 + rank`, suit 0 ♦, 1 ♣, 2 ♠, 3 ♥; rank 0..12 = 2..A. The deck has 32 cards
  (ranks 5..12 = 7..A): `deck[i] = i % 8 + 5 + (i / 8) * 13`. Hearts are cards ≥ 39 (`> 0x27`),
  the king of hearts is 50 (`0x32`), queens have `rank == 10`, "boys" have `rank in [9, 11]`
  (J, K).
- Sprite index = card number (KING.LIB keeps empty slots for 2..6); the back is `ds:0002` = 54.
- Players 1..4: 1 left, 2 top, 3 right, 4 the human (bottom). Per player `p`, 20-byte rows of
  words: `ds:0682 + 20p` = `[0]` count, `[1..8]` cards, `[9]` (`ds:0694 + 20p`) the card on the
  table this trick; `ds:06d2 + 20p` = screen x of each card. (So the human's hand starts at
  `ds:06d2`, and `ds:0722` are its x positions.)

## Game flow (KING.EXE, short variant)

`main` (`6388`): `Randomize`, `load_resources`, `init_graphics`, `title_and_login` (registration,
password, pick 3 of 12 partners), then 14 deals `deal_no` (`ds:0746`) = 0..13, then `game_over`.

- Deal: `deal` builds and shuffles the deck, then deals 32 cards one at a time starting with
  player `dealer_counter % 4 + 1` (`ds:0738`, +1 per card and +1 per deal, never reset), each
  card animated. The human's hand is sorted (selection sort by card number). The first leader
  is `ds:073a` = `ds:0738` after dealing, i.e. the player who got the first card (32 ≡ 0 mod 4).
- Contract = `deal_no % 7`; deals 0..6 are "не брать" (price negative), 7..13 "брать" (price
  positive). `items_left` (`ds:073c`) counts the penalty items; the deal ends when it is 0 (so
  "Кинг" ends as soon as the K♥ falls).

| # | Name          | Item                       | Price | Items |
|---|---------------|----------------------------|-------|-------|
| 0 | ВЗЯТКИ        | each trick                 | 20    | 8     |
| 1 | ЧЕРВИ         | each heart                 | 20    | 8     |
| 2 | МАЛЬЧИКИ      | each J, K                  | 20    | 8     |
| 3 | ДЕВОЧКИ       | each Q                     | 40    | 4     |
| 4 | 2 ПОСЛЕДНИЕ   | tricks 7 and 8             | 80    | 2     |
| 5 | КИНГ          | K♥                         | 160   | 1     |
| 6 | ВСЕ ПОДРЯД    | all of the above (ералаш)  | 20 per trick + 20/heart + 20/boy + 40/queen + 80/last-two trick + 160/K♥ | 8 tricks |

- Play: `play_deal` (`61f4`) loops `play_card(leader_counter % 4 + 1)`; the trick's best card is
  `trick_best` (`ds:0744`, card + 256 * player): replaced when the new card is the same suit as
  the led card and higher. No trumps. After four cards `finish_trick` (`1a56`) takes the
  highest card of the led suit, `score_trick` (`1740`) adds the price to `deal_score[winner]`,
  and the winner leads (`ds:073a` = winner - 1).
- Rules enforced for both the human and the AI: follow suit; in contracts {1, 5, 6} (hearts,
  king, eralash) hearts may not be led while holding another suit (set at `1000:1137`).
- `deal_summary` (`3f01`) adds `deal_score` to `total_score` (`ds:05da`).

## AI (`ai_choose_card` 0e88, `ai_search` 0afd, `ai_trick_value` 0901)

The computer sees every hand (the partner cards say "ОН ВСЕГДА МУХЛЮЕТ").

`ai_choose_card(p)`:

1. Search depth in tricks from `trick_no` (`ds:0740`): tricks 0..3 → 2, 4..5 → 3, 6 → 2,
   7..8 → 1 **[verify]** the exact meaning of a depth (the search stops when the number of
   tricks completed inside the search equals it).
2. With one card left, play it.
3. Leading in contracts {1, 5, 6}: only non-hearts are candidates if the hand has any
   (set at `1000:0e68`).
4. Following: take the highest card of the led suit. If there is one and it is **lower than
   the current best card of the trick, play it at once** (no search: duck with the highest
   card that cannot win). If there is one otherwise, only led-suit cards are candidates.
5. Otherwise run `ai_search` for each candidate in hand order and keep the lowest value;
   ties keep the first.

`ai_search` is a plain minimax over all four real hands: at player `p`'s turns the value is
minimised, at everyone else's maximised (paranoid). It applies the same follow-suit and
no-hearts-lead rules, tracks the trick winner, and at the end of each trick adds
`ai_trick_value` to the winner's running score. It returns `p`'s score. The whole state
(80 bytes) is copied per node.

`ai_trick_value` (`0901`) is **not** the real scoring (`score_trick`, `1740`). The port must
copy it as it is:
- the prices are fixed in code (20 / 20 / 20 / 40 / 160), not `item_price`;
- contract 4 ("2 ПОСЛЕДНИЕ") is valued as 20 for *every* trick, not 80 for tricks 7 and 8;
- contract 6 (ералаш) has no last-two part (20 per trick, 20 per heart, 20 per boy, 40 per
  queen, 160 for the K♥);
- the value is positive in deals 0..6 and negated in 7..13, so `p` always minimises.

Some branches test `SetIn` results that Ghidra drops (`bVar = true` artifacts); read those in
the disassembly. The choice of partner does not affect play **[verify]** (no reference to
`ds:05e4..05e8` in the AI).

## Login and KING.OVL

`login` (`243f`) asks for the "subscription index" of Komsomolskaya Pravda (`50057`, 3 tries,
else quit: the copy protection, compared case-insensitively), then name and 4-character
password. KING.OVL is a `file of` 32-byte records:

| Offset | Type       | Field                                         |
|--------|------------|-----------------------------------------------|
| 0      | string[12] | name, chars XOR 0x1A                          |
| 13     | string[4]  | password, chars XOR 0x1A                      |
| 18     | 8 bytes    | unused                                        |
| 26     | longint    | balance ("лицевой счёт", dollars in the chronicle) |
| 30     | word       | games played                                  |

`game_over` (`450e`) adds the human's total to the balance, rewrites the record, prints the
"СВЕТСКАЯ ХРОНИКА" society column and the top 9 of "НАШИ МИЛЛИОНЕРЫ".

## Data files

- `KING.LIB`: 128-byte header (`[0]` = 72 entries, `[1..72]` size of each in 128-byte records),
  then the sprites. Sprite: `u16 width, u16 height, u16 ?`, then per row `u16 length` and RLE
  (`b ≥ 0x80`: `b - 0x80` copies of the next byte; else `b` literal bytes), one byte per pixel.
  Drawn by `put_sprite` (`6430`) with a transparent colour argument. Contents: 32 cards, back,
  partner portraits (3 strips of 4 and a 12-face grid), logos, money, hands.
- `KING.FNT`: three 8-pixel-wide fonts of 256 glyphs, 6, 8 and 14 rows (1536 + 2048 + 3584
  bytes), CP866.
- `KING.HLP`: CP866 text shown on the title page (author, rules).
- `re/tools/king.py` decodes all three (and the OVL).

## KING2.EXE (full variant) — the port target

Same engine and the same routines as KING.EXE except the ones below; names in
`ghidra/names_king2.txt` (DGROUP variables from `ds:051e` up are KING's + 0x78, because KING2
inserts the 120-byte `games_played` array there). `wait_space_or_click` takes a timeout in 10 ms
steps (0 = wait for ever).

- Every player has the same 14 games (7 contracts x "не брать"/"брать"); game `g` = `half * 7 +
  contract`. `games_played` (`ds:0500 + 30p + 2g`) and `games_left` (`ds:051c + 30p`, starts at
  14). The game ends when all four counts are 0: 56 deals.
- `deal` (`685c`) shuffles and deals exactly as KING.EXE, then calls `contract_screen` (`60cd`)
  for the **declarer** `dealer_counter % 4 + 1` (`ds:07b0`), i.e. the player who got the first
  card; the declarer also leads first. `dealer_counter` grows by 32 per deal plus 1 in `main`,
  so the declarer rotates 1, 2, 3, 4, 1, ...
- The choice is made after the deal, with the hand known. `choose_contract(p)` (`57bc`) sets
  `games_played[p][g] = 1`, `games_left[p]--`, `deal_no` (`ds:07be`) = `g`; `contract_screen`
  then sets the price and item count exactly as KING's `draw_contract_panel`.
- Human (p = 4): cursor on the 2x7 grid (Left/Right = half, Up/Down = contract, or the mouse),
  Space / click accepts only an unplayed game (otherwise a beep). Starts at "не брать / ВЗЯТКИ".
  No random numbers.
- Computer: `v = Σ (card % 13) - 68` over its 8 cards (68 = 8 x the average rank 8.5; a weak hand
  is negative). Then, taking the first group that still has an unplayed game and picking in it
  by `Random(n)` repeated until it hits an unplayed one:
  1. if `v < 0`: "не брать" — `{0,1,2}` by `Random(3)` (only if `v > -10`), else `{3..6}` by
     `Random(4) + 3`, else `{0..6}` by `Random(7)`;
  2. otherwise (or if nothing was left there) "брать" — `{0,1,2}` by `Random(3)` (only if
     `v < 10`), else `{3..6}` by `Random(4) + 3`, else `{0..6}` by `Random(7)`;
  3. otherwise `Random(14)` over all 14 games. Unreachable: the declarer rotates evenly, so a
     player is declarer exactly 14 times and always has an unplayed game, and step 2's last group
     finds any unplayed "брать" game. The port keeps it anyway.
  Then it shows "Выбираю..." and waits `wait_space_or_click(500)` (5 s or a key).
- Random calls added to the list above: the computer's contract choice. Rejected draws in the
  `repeat ... until` loops count too.
