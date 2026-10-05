# KING · Кинг (1993) — decompilation & multiplatform port

«Кинг» is the trick-taking card game that *Комсомольская правда* gave its readers in 1993, by
Д. (В.) Башуров ("Bady") of Arzamas-16 — on half the PCs of the former USSR. Four players,
32 cards, 14 games: seven where you must not take (tricks, hearts, the J and K «мальчики», the
queens «девочки», the last two tricks, the king of hearts, all of them at once) and the same
seven where you must. This repo is a reverse-engineered port of the full variant, `KING2.EXE`,
to portable C + SDL2: it runs natively and in a browser via WebAssembly.

**Play in the browser:** <https://king.rembi.sh/> — with the club's global table of millionaires.

[<img src="https://github.com/rembish/king-card-game/releases/download/v0.1.0/king.gif" width="480" alt="Choosing partners, the deal, and a game of «мальчики»">](https://github.com/rembish/king-card-game/releases/download/v0.1.0/king.mp4)

*Click for the video with the PC speaker. Made with `tools/clips.sh` from your own copy of the
game (the demo plays your seat at a human's pace); the clips live in the releases, never in
the repository.*

The goal has two halves:

1. **Game logic: decompiled faithfully.** The Turbo Pascal random number generator and every
   call to it, the shuffle and the deal, the contract choice of the computer players, their
   search, the scoring and even the human's cursor (it draws random numbers while it blinks) —
   a deterministic C core, checked move by move against the original machine code (see
   [How faithful is it?](#how-faithful-is-it)).
2. **Presentation: the original's own.** The port draws with the original's cards, faces and
   bitmap fonts, read at run time from *your own copy* of the game. Nothing of the original is
   in this repository.

## The original

The tools in `re/` and the game itself need your own copy of the DOS release in `original/`
(git-ignored, never distributed):

```
f8138fec4fa888f7f0c875660e55fb2c44d7263f50fd6718690dba02649d091e  KING.EXE   v1.7 "Короткий вариант"
a86179169d78e7976e15f0ed64e80187dde40d4a33d176c699e3301e5c7ae19e  KING2.EXE  v1.1 "Полный вариант"
627e4fe496a985d8c6cc43bba5a89e7b6947093cff9bf3f77effe89723054dbe  KING.LIB   sprites
c97a2a69ebe88577d7453408bf92a1739ed6d7b5a8c010f1d3078b5b9c9dd364  KING.FNT   fonts
```

- Turbo Pascal 5.x/6.0 with the Graph unit's EGA driver linked in: 640x350, 16 colours.
- `KING.EXE` plays the 14 games in a fixed order; `KING2.EXE` lets each player in turn choose
  one of its unplayed games after seeing its cards (56 deals). The port follows `KING2.EXE`.
- `KING.LIB` holds the pictures (RLE sprites), `KING.FNT` three 8-pixel CP866 fonts, `KING.OVL`
  the club's members. All rules are in the executable.
- The game asks for the subscription index of «Комсомолка» at the start and then a password.
  The port leaves both out.

## Playing

| Key | Action |
|-----|--------|
| ← → | choose a card |
| Space / Enter | play it (or click the card) |
| ↑ ↓ ← → Space | choose your game in the grid (or click it) |
| Space / click | go on after a trick or a deal |
| Esc | back to the title |
| F11 / Alt+Enter | full screen |

You join the club under a name (no password). Your «лицевой счёт» grows or shrinks with every
game, and after it the society column «СВЕТСКАЯ ХРОНИКА» reports on your evening and lists «НАШИ
МИЛЛИОНЕРЫ». Natively the club lives in the user data folder (`~/.local/share/King/King/` on
Linux).

On king.rembi.sh the club is global (`cloudflare/`, Cloudflare Pages Functions and KV): names
are unique (the first browser to take one keeps it), each game is dealt with a seed the server
signs, and at the end the browser sends its keyboard polls. The server replays the whole game
with the same C core compiled to WebAssembly and books the total it gets itself, so a score
cannot be typed in — only played (the computer partners cheat; the humans may try to).

The three partners are a matter of taste: who you pick does not change how they play.

## Building

Native (Linux, macOS, Windows with any SDL2 package):

```sh
cmake -S . -B build
cmake --build build -j
./build/king            # looks for KING.LIB and KING.FNT in original/, ., or a folder you name
```

Browser (Emscripten):

```sh
source ~/tools/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web
cmake --build build-web -j --target king
cd build-web && python3 -m http.server   # open http://localhost:8000/king.html
```

The page needs `KING.LIB` and `KING.FNT`. With one click (and the player's consent) it fetches
the game's zip from [MyAbandonware](https://www.myabandonware.com/game/king-c1a) through
`/api/original`, which asks for the current link the way their download button does and passes
the 90 KB through (their server lets no other page download it; nothing is kept). Or the player
chooses their own files or zip. Either way they stay in the browser.

## How faithful is it?

- `re/emu/kemu.py` loads the original `KING2.EXE` into the Unicorn CPU emulator, stubs out only
  drawing, sound and delays, scripts the keyboard, and plays whole games through the original's
  own routines, checking the rules on the way. `re/emu/record.py` stores each game: the deals,
  the contract choices, every card, every poll of the keyboard, the scores.
- `tests/difftest.py` replays those games through `core/` (`build/replay`) and compares them deal
  by deal, random numbers included. `tests/frontend.py` plays them through the frontend's own
  input loops (`build/king --replay`).
- `tests/mutants.py` breaks the core in 22 small ways (the shuffle, prices, thresholds, the
  cursor blink, the search) and checks that the difftest catches every one.

```sh
uv sync                                       # unicorn, capstone, pillow in .venv
.venv/bin/python re/emu/record.py 1 2 3       # ~100 s per game
cmake --build build && .venv/bin/python tests/difftest.py && .venv/bin/python tests/frontend.py
.venv/bin/python tests/mutants.py
```

One finding stands out: the computer players see every hand and run a minimax search, but its
evaluation reads the running scores where it means to read the cards of the trick. In hearts,
boys, queens and the king they therefore just play their first legal card; only in tricks,
the last two and «все подряд» does the search matter. The port plays exactly like that.
Findings and addresses are in [`re/NOTES.md`](re/NOTES.md).

## The server

```sh
cloudflare/build.sh                  # the web build into cloudflare/public, the core into lib/core.wasm
node --test cloudflare/test.mjs      # replays the recorded games in tests/fixtures, tokens, names
cd cloudflare && npx wrangler pages dev public --kv CLUB   # needs KING_SECRET in .dev.vars
```

Pushes to `main` deploy through GitHub Actions (`.github/workflows/pages.yml`, with the
`CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` repository secrets).

## Tooling

| Tool | Used for |
|------|----------|
| Ghidra (headless), `re/ghidra/run.sh` | Decompiling both executables; names in `re/ghidra/names_*.txt` |
| Python 3 + Unicorn, capstone, Pillow (`uv sync`) | The emulator harness, disassembly, `re/tools/king.py` (data decoders) |
| gcc + SDL2 + CMake | Native build |
| Emscripten (emsdk) | WebAssembly build |

## Credits

«Кинг» © 1993 Дмитрий Башуров (Bady), Arzamas-16; published by «Комсомольская правда». This
is an unofficial fan reimplementation for preservation; no original game files are distributed.

The code of this port is under the [BSD 3-Clause License](LICENSE).
