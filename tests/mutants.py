"""Give the difftest teeth: build the core with one deliberate change at a time and check that
tests/difftest.py notices. Each mutant is a (description, old, new) edit of core/kg_core.c.

    python3 tests/mutants.py [SEED...]
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "core", "kg_core.c")

ONLY = os.environ.get("ONLY")

MUTANTS = [
    ("rng multiplier", "0x08088405u", "0x08088404u"),
    ("fewer shuffle swaps", "k < 1000", "k < 999"),
    ("human hand unsorted", "if (h[j] < h[i - 1])", "if (0)"),
    ("declarer shifted", "return g->dealer % 4 + 1;", "return (g->dealer + 1) % 4 + 1;"),
    ("contract threshold", "if (v > -10)", "if (v > -11)"),
    ("take threshold", "if (v < 10)", "if (v < 11)"),
    ("no random fallback", "do k = kg_random(g, 14);", "do k = (k + 1) % 14 + 0 * kg_random(g, 14);"),
    ("queen price", "{20, 20, 20, 40, 80, 160, 20}", "{20, 20, 20, 30, 80, 160, 20}"),
    ("last two = 6,7", "trick == 7 || trick == 8", "trick == 6 || trick == 7"),
    ("boys = J, Q", "rank == 9 || rank == 11", "rank == 9 || rank == 10"),
    ("eralash king x4", "g->price * 8", "g->price * 4"),
    (
        "hearts may be led",
        "contract == 1 || contract == 5 || contract == 6",
        "contract == 1 || contract == 5",
    ),
    ("blink 10 passes", "if (g->blink > 11)", "if (g->blink > 10)"),
    (
        "blink draws always",
        "if (g->blink == 0 && kg_random(g, 20) == 19)",
        "if (kg_random(g, 20) == 19 && g->blink == 0)",
    ),
    ("cursor not retried", "while (KG_SUIT(h[g->cursor]) != led);", "while (0);"),
    ("ai depth", "{2, 2, 2, 2, 3, 3, 2, 1}", "{2, 2, 2, 3, 3, 3, 2, 1}"),
    ("ai ties keep last", "if (v < pick_v)", "if (v <= pick_v)"),
    ("ai no duck shortcut", "if (hi_i > 0 && best > hi) return hi_i;", ""),
    ("ai hearts threshold", "if (s[p][9] > 0x27) v += 20;", "if (s[p][9] > 19) v += 20;"),
    ("ai eralash bonus", "if (c == 6) v += 20;", "if (c == 6) v += 40;"),
    ("ai sign", "return x->contract_game < 7 ? v : (int16_t)-v;", "return v;"),
    ("opponents minimise", "if (q == x->root ? r < v : r > v) v = r;", "if (r < v) v = r;"),
]


def main() -> None:
    seeds = sys.argv[1:]
    tmp = tempfile.mkdtemp(prefix="kg-mut-")
    original = open(SRC).read()
    survived = []
    try:
        for name, old, new in MUTANTS:
            if ONLY and ONLY not in name:
                continue
            if old not in original:
                print(f"{name}: pattern not found")
                survived.append(name)
                continue
            src = os.path.join(tmp, "kg_core.c")
            open(src, "w").write(original.replace(old, new, 1))
            exe = os.path.join(tmp, "replay")
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-std=c99",
                    "-I",
                    os.path.join(ROOT, "core"),
                    "-o",
                    exe,
                    src,
                    os.path.join(ROOT, "tests", "replay.c"),
                ],
                check=True,
            )
            try:
                r = subprocess.run(
                    [sys.executable, os.path.join(ROOT, "tests", "difftest.py"), exe, *seeds],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                caught, how = r.returncode != 0, ""
            except subprocess.TimeoutExpired:
                caught, how = True, " (hangs)"
            print(f"{name}: {'caught' + how if caught else 'SURVIVED'}", flush=True)
            if not caught:
                survived.append(name)
    finally:
        shutil.rmtree(tmp)
    print(f"{len(MUTANTS) - len(survived)}/{len(MUTANTS)} caught")
    sys.exit(1 if survived else 0)


if __name__ == "__main__":
    main()
