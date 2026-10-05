"""Play KING2 games in the emulator with a random-key human and store the event logs.

    python3 re/emu/record.py SEED [SEED...]     -> re/emu/logs/seed-SEED.json

A log is what the C core must reproduce: per deal the seed before it, the hands as stored
(computer hands in deal order, the human's sorted), the game chosen, every poll of the keyboard
(null = no key) during the contract choice and the play, the tricks and the scores.
"""

import json
import os
import random
import sys
from typing import Any

from kemu import Game, Key

HERE = os.path.dirname(os.path.abspath(__file__))
MOVES: list[list[Key]] = [[None], [None, None], ["\0", "K"], ["\0", "M"], ["\0", "H"], ["\0", "P"], [" "]]


def record(seed: int) -> dict[str, Any]:
    g = Game()
    g.setup(seed)
    rnd = random.Random(seed)
    g.key_source = lambda: list(rnd.choice(MOVES))
    deals = []
    while g.games_left():
        deals.append(g.play_one_deal())
    g.keys.clear()
    return {"seed": seed, "deals": deals}


if __name__ == "__main__":
    os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
    for a in sys.argv[1:]:
        log = record(int(a))
        path = os.path.join(HERE, "logs", f"seed-{a}.json")
        with open(path, "w") as f:
            json.dump(log, f, separators=(",", ":"))
        print(path, len(log["deals"]), "deals, totals", log["deals"][-1]["totals"])
