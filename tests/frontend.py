"""Play each recorded game through the frontend's own code (build/king --replay): its poll
pacing, input flushes and hand bookkeeping must give the original's totals and log exactly the
polls it was fed.

    python3 tests/frontend.py [build/king] [SEED...]
"""
import glob
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from difftest import polls  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

if __name__ == '__main__':
    args = sys.argv[1:]
    exe = args.pop(0) if args and not args[0].isdigit() else os.path.join(ROOT, 'build', 'king')
    paths = [os.path.join(ROOT, 're', 'emu', 'logs', f'seed-{s}.json') for s in args] or \
        sorted(glob.glob(os.path.join(ROOT, 're', 'emu', 'logs', 'seed-*.json')))
    bad = 0
    for p in paths:
        log = json.load(open(p))
        script = polls(log)
        r = subprocess.run([exe, '--replay', str(log['seed']), script], capture_output=True, text=True)
        out = r.stdout.splitlines()
        want = log['deals'][-1]['totals']
        err = None
        if r.returncode or len(out) < 2:
            err = f'exit {r.returncode}: {r.stderr.strip()}'
        elif [int(x) for x in out[0].split()] != want:
            err = f'totals {out[0]}, original {want}'
        elif out[1] != script:
            err = 'poll log differs from the polls fed'
        print(os.path.basename(p), 'OK' if err is None else 'FAIL: ' + err)
        bad += err is not None
    sys.exit(1 if bad else 0)
