"""Play a whole KING2 game in the emulator with a random-key human; print what happens."""
import random
import sys

from kemu import DEAL_NO, Game

seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
g = Game()
g.setup(seed)
rnd = random.Random(seed)
g.key_source = lambda: rnd.choice([[None], [None, None], ['\0', 'K'], ['\0', 'M'], ['\0', 'H'],
                                   ['\0', 'P'], [' ']])
plays = []
g.on_hand = lambda p: plays.append(p)
n = 0
while g.games_left():
    g.play_one_deal()
    n += 1
    print(f'deal {n:2}: game {g.r16(DEAL_NO):2}  totals {g.totals()}  seed {g.ru32(0x418):08x}')
print('deals', n)
