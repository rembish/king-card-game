"""Compare the core (build/replay) with the original's logs (re/emu/logs, made by record.py).

python3 tests/difftest.py [build/replay] [SEED...]      default: every log there is
"""

import glob
import json
import os
import subprocess
import sys
from typing import Any

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = [
    "seed",
    "dealer",
    "hands",
    "game",
    "declarer",
    "seed_play",
    "tricks",
    "deal_score",
    "totals",
    "seed_end",
]


def polls(log: dict[str, Any]) -> str:
    out = []
    keys = [k for d in log["deals"] for k in d["contract_keys"] + d["play_keys"]]
    i = 0
    while i < len(keys):
        k = keys[i]
        if k is None:
            out.append(".")
        elif k == " ":
            out.append("_")
        elif k == "\0":
            i += 1
            out.append(keys[i])
        else:
            out.append("?")
        i += 1
    return "".join(out)


def check(exe: str, path: str) -> str | None:
    log = json.load(open(path))
    r = subprocess.run([exe, str(log["seed"]), polls(log)], capture_output=True, text=True)
    mine = [json.loads(line) for line in r.stdout.splitlines()]
    if r.returncode or r.stderr:
        return f"replay failed: {r.stderr.strip()}"
    for n, (a, b) in enumerate(zip(log["deals"], mine, strict=False), 1):
        for f in FIELDS:
            if a[f] != b[f]:
                return f"deal {n}: {f} differs\n  original {a[f]}\n  core     {b[f]}"
    if len(mine) != len(log["deals"]):
        return f"{len(mine)} deals, original {len(log['deals'])}"
    return None


if __name__ == "__main__":
    args = sys.argv[1:]
    exe = args.pop(0) if args and not args[0].isdigit() else os.path.join(ROOT, "build", "replay")
    paths = [os.path.join(ROOT, "re", "emu", "logs", f"seed-{s}.json") for s in args] or sorted(
        glob.glob(os.path.join(ROOT, "re", "emu", "logs", "seed-*.json"))
    )
    bad = 0
    for p in paths:
        err = check(exe, p)
        print(os.path.basename(p), "OK" if err is None else "FAIL: " + err)
        bad += err is not None
    sys.exit(1 if bad else 0)
