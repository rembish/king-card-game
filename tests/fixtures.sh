#!/bin/sh
# The three games in tests/fixtures/games.json (recorded from the original KING2.EXE) through
# kg_replay (build/verify) and the frontend's own loops (build/king --replay). No game files
# needed: CI runs it.
set -e
BUILD=${1:-build}
python3 - "$BUILD" <<'PY'
import json, subprocess, sys
build, bad = sys.argv[1], 0
for g in json.load(open('tests/fixtures/games.json')):
    want = ' '.join(str(t) for t in g['totals'])
    got = subprocess.run([f'{build}/verify', str(g['seed']), g['polls']], capture_output=True, text=True).stdout.strip()
    rep = subprocess.run([f'{build}/king', '--replay', str(g['seed']), g['polls']], capture_output=True,
                         text=True).stdout.split('\n')
    ok = got == '0 ' + want and rep[0] == want and rep[1] == g['polls']
    print(f"game {g['seed']}:", 'OK' if ok else f'FAIL {got} / {rep[0]}')
    bad += not ok
sys.exit(bad)
PY
