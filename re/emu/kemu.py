"""Run the game logic of the original KING2.EXE headless under Unicorn.

The image is loaded at segment 0x1000 (same addresses as the Ghidra project) and relocated. The
program body is not run: `Game` repeats what `main` does between the login and the end (deal,
play_deal, deal_summary until every player has played all 14 games) by calling the original
routines. Drawing, sound and delays are stubbed; the keyboard is a script, so the human's own
routines (card cursor, contract grid) run as they are and draw the same random numbers.

Pascal calling convention: arguments are pushed in source order and the callee pops them.
"""

import os
import struct
from collections.abc import Callable
from typing import TYPE_CHECKING, Any

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16
from unicorn.x86_const import (
    UC_X86_INS_IN,
    UC_X86_INS_OUT,
    UC_X86_REG_AX,
    UC_X86_REG_BP,
    UC_X86_REG_CS,
    UC_X86_REG_DS,
    UC_X86_REG_ES,
    UC_X86_REG_IP,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
)

if TYPE_CHECKING:
    # The same class as unicorn.Uc, which mypy sees untyped: the package root picks its Python 2
    # or 3 module on sys.version_info.major, a test mypy does not evaluate (as in ../elite-plus).
    from unicorn.unicorn_py3.unicorn import Uc as Uc
else:
    from unicorn import Uc as Uc

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "..", "original", "KING2.EXE")

CS = 0x1000
DS = 0x1D2E
CRT, GRAPH, SYSTEM = 0x1BA3, 0x1871, 0x1C05
STACK_SEG = 0x1E29  # SS from the EXE header
STACK_TOP = 0x7D00
SENTINEL = (0x9000, 0x0000)  # far return address that ends a call (unused memory)

# Routines replaced by a bare return (bytes popped). Each one only draws, plays sound or waits;
# none writes a variable the game logic reads (checked in the decompiled callees).
STUBS = {
    (CS, 0x70E4): ("put_sprite", "ret", 0x0E),
    (CS, 0x0295): ("draw_text", "retf", 0x12),
    (CS, 0x057B): ("fill_rect_bytes", "retf", 8),
    (CS, 0x0615): ("copy_rect_bytes", "retf", 8),
    (CS, 0x06A9): ("animate_card", "retf", 0x0C),
    (CS, 0x1F98): ("draw_box", "retf", 0x0A),
    (CS, 0x567B): ("draw_counter", "retf", 6),
    (CS, 0x575C): ("draw_counters", "retf", 0),
    (CS, 0x67DA): ("flash_contract", "retf", 0),
    (CS, 0x066E): ("flush_input", "retf", 0),
    (CS, 0x3EB9): ("wait_space_or_click", "retf", 2),
    (CRT, 0x029E): ("Delay", "retf", 2),
    (CRT, 0x02C6): ("Sound", "retf", 2),
    (CRT, 0x02F3): ("NoSound", "retf", 0),
    (GRAPH, 0x0D05): ("SetPalette", "retf", 4),
}

# names (KING2 addresses)
DEAL, PLAY_DEAL, DEAL_SUMMARY = 0x685C, 0x6E1D, 0x3F36
DRAW_HAND, KEYPRESSED, READKEY, QUIT = 0x159D, 0x02FA, 0x030C, 0x0000

# DGROUP variables (KING2)
RANDSEED = 0x0418
GAMES_PLAYED = 0x0500  # + 30p + 2g
GAMES_LEFT = 0x051C  # + 30p
DEAL_SCORE, TOTAL_SCORE = 0x064A, 0x0652  # + 2p
HANDS = 0x06FA  # + 20p: count, cards[1..8], table card [9]
DEALER, DEAL_NO = 0x07B0, 0x07BE
MOUSE = 0x0596


Key = str | None  # one poll's answer: the char read, or None for "no key"
Record = dict[str, Any]  # one deal's events, as stored in the JSON logs


class Quit(Exception):
    pass


class King:
    def __init__(self) -> None:
        raw = open(EXE, "rb").read()
        hdr = struct.unpack("<H", raw[8:10])[0] * 16
        img = bytearray(raw[hdr:])
        nrel = struct.unpack("<H", raw[6:8])[0]
        rtab = struct.unpack("<H", raw[0x18:0x1A])[0]
        for k in range(nrel):
            off, seg = struct.unpack("<HH", raw[rtab + 4 * k : rtab + 4 * k + 4])
            p = seg * 16 + off
            v = struct.unpack("<H", img[p : p + 2])[0]
            img[p : p + 2] = struct.pack("<H", (v + CS) & 0xFFFF)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(CS * 16, bytes(img))
        self.keys: list[Key] = []  # script: a char (str of length 1) or None = one idle poll
        self.key_source: Callable[[], list[Key]] | None = None  # refills an empty script
        self.max_insns = 20_000_000_000  # per call; only a guard against endless loops
        self.on_hand: Callable[[int], None] | None = None  # when draw_hand runs
        self.consumed: list[Key] = []  # every poll answered: None (no key) or the char read
        self.error: BaseException | None = None
        for (seg, off), (_name, kind, n) in STUBS.items():
            code = (
                (b"\xc2" if kind == "ret" else b"\xca") + struct.pack("<H", n)
                if n
                else (b"\xc3" if kind == "ret" else b"\xcb")
            )
            mu.mem_write(seg * 16 + off, code)
        self.pyfuncs: dict[tuple[int, int], tuple[Callable[[list[int]], int], int]] = {
            (CRT, KEYPRESSED): (self.keypressed, 0),
            (CRT, READKEY): (self.readkey, 0),
            (CS, DRAW_HAND): (self.draw_hand, 2),
            (CS, QUIT): (self.quit, 0),
        }
        for seg, off in self.pyfuncs:
            a = seg * 16 + off
            mu.hook_add(UC_HOOK_CODE, self._py, begin=a, end=a)
        mu.hook_add(UC_HOOK_INTR, self._intr)
        mu.hook_add(UC_HOOK_INSN, self._io, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INSN, self._io, None, 1, 0, UC_X86_INS_IN)
        self.w16(MOUSE, 0)

    # ---- memory (DGROUP) ----
    def rb(self, off: int, n: int = 1) -> bytes:
        return bytes(self.mu.mem_read(DS * 16 + off, n))

    def r16(self, off: int) -> int:
        v: int = struct.unpack("<h", self.rb(off, 2))[0]
        return v

    def ru32(self, off: int) -> int:
        v: int = struct.unpack("<I", self.rb(off, 4))[0]
        return v

    def wb(self, off: int, data: bytes) -> None:
        self.mu.mem_write(DS * 16 + off, bytes(data))

    def w16(self, off: int, v: int) -> None:
        self.wb(off, struct.pack("<H", v & 0xFFFF))

    def w32(self, off: int, v: int) -> None:
        self.wb(off, struct.pack("<I", v & 0xFFFFFFFF))

    def hand(self, p: int) -> list[int]:
        n = self.r16(HANDS + 20 * p)
        return [self.r16(HANDS + 20 * p + 2 * i) for i in range(1, n + 1)]

    # ---- replaced routines ----
    def keypressed(self, args: list[int]) -> int:
        if not self.keys and self.key_source:
            self.keys += self.key_source()
        if self.keys and self.keys[0] is None:
            self.keys.pop(0)
            self.consumed.append(None)
            return 0
        if not self.keys:
            raise RuntimeError("key script exhausted")
        return 1

    def readkey(self, args: list[int]) -> int:
        k = self.keys.pop(0) if self.keys else None
        if k is None:
            raise RuntimeError("ReadKey without a scripted key")
        self.consumed.append(k)
        return ord(k)

    def draw_hand(self, args: list[int]) -> int:
        if self.on_hand:
            self.on_hand(args[0])
        return 0

    def quit(self, args: list[int]) -> int:
        raise Quit()

    def _intr(self, mu: Uc, intno: int, _: object) -> None:
        ip = mu.reg_read(UC_X86_REG_IP)
        cs = mu.reg_read(UC_X86_REG_CS)
        raise RuntimeError(f"unhandled int {intno:02x} at {cs:04x}:{ip:04x}")

    def _io(self, mu: Uc, port: int, size: int, value: object = None, _: object = None) -> None:
        cs = mu.reg_read(UC_X86_REG_CS)
        ip = mu.reg_read(UC_X86_REG_IP)
        raise RuntimeError(f"port I/O {port:x} at {cs:04x}:{ip:04x}: a drawing routine is not stubbed")

    def _py(self, mu: Uc, addr: int, size: int, _: object) -> None:
        seg = mu.reg_read(UC_X86_REG_CS)
        fn, nbytes = self.pyfuncs[(seg, addr - seg * 16)]
        sp = mu.reg_read(UC_X86_REG_SP)
        ss = mu.reg_read(UC_X86_REG_SS)

        def rd(o: int) -> int:
            v: int = struct.unpack("<H", bytes(mu.mem_read(ss * 16 + ((sp + o) & 0xFFFF), 2)))[0]
            return v

        ip, cs = rd(0), rd(2)
        args = [rd(4 + nbytes - 2 - 2 * k) for k in range(nbytes // 2)]  # source order
        try:
            ax = fn(args)
        except (Quit, RuntimeError, AssertionError) as e:  # reported by call()
            self.error = e
            mu.emu_stop()
            return
        mu.reg_write(UC_X86_REG_AX, (ax or 0) & 0xFFFF)
        mu.reg_write(UC_X86_REG_SP, (sp + 4 + nbytes) & 0xFFFF)
        mu.reg_write(UC_X86_REG_CS, cs)
        mu.reg_write(UC_X86_REG_IP, ip)

    # ---- calling ----
    def call(self, off: int, *args: int, seg: int = CS) -> int:
        """Far call seg:off with Pascal arguments (source order)."""
        mu = self.mu
        mu.reg_write(UC_X86_REG_DS, DS)
        mu.reg_write(UC_X86_REG_ES, DS)
        mu.reg_write(UC_X86_REG_SS, STACK_SEG)
        sp = STACK_TOP

        def push(v: int) -> None:
            nonlocal sp
            sp -= 2
            mu.mem_write(STACK_SEG * 16 + sp, struct.pack("<H", v & 0xFFFF))

        for a in args:
            push(a)
        push(SENTINEL[0])
        push(SENTINEL[1])
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_BP, 0)
        mu.reg_write(UC_X86_REG_CS, seg)
        self.error = None
        mu.emu_start(seg * 16 + off, SENTINEL[0] * 16 + SENTINEL[1], count=self.max_insns)
        if self.error:
            raise self.error
        here = mu.reg_read(UC_X86_REG_CS) * 16 + mu.reg_read(UC_X86_REG_IP)
        if here != SENTINEL[0] * 16 + SENTINEL[1]:
            raise RuntimeError(f"call {seg:04x}:{off:04x} stopped at {here:05x}")
        ax: int = mu.reg_read(UC_X86_REG_AX)
        return ax


PLAY_CARD = 0x6C02
ITEMS_LEFT, LEADER = 0x07B4, 0x07B2
NO_HEART_LEAD = {1, 5, 6}


def suit(c: int) -> int:
    return c // 13


class RuleError(AssertionError):
    pass


class Game(King):
    """A game as KING2's main plays it after the login: 56 deals.

    `play_one_deal` returns the deal's event record and checks the rules on the way (hands are
    a permutation of the 32 cards, plays come from the hand and follow suit, no hearts lead in
    contracts 1, 5, 6 while other suits are held, the highest card of the led suit leads next,
    no items left at the end).
    """

    def setup(self, seed: int) -> None:
        self.w32(RANDSEED, seed)
        for p in range(1, 5):
            for g in range(14):
                self.w16(GAMES_PLAYED + 30 * p + 2 * g, 0)
            self.w16(GAMES_LEFT + 30 * p, 14)
            self.w16(DEAL_SCORE + 2 * p, 0)
            self.w16(TOTAL_SCORE + 2 * p, 0)
        self.w16(DEALER, 0)

    def games_left(self) -> int:
        return sum(self.r16(GAMES_LEFT + 30 * p) for p in range(1, 5))

    def play_one_deal(self) -> Record:
        rec: Record = {"seed": self.ru32(RANDSEED), "dealer": self.r16(DEALER)}
        self.consumed = []
        self.on_hand = None
        self.call(DEAL)
        hands = {p: self.hand(p) for p in range(1, 5)}
        deck = sorted(c for h in hands.values() for c in h)
        if deck != sorted(r * 13 + k for r in range(4) for k in range(5, 13)) or any(
            len(h) != 8 for h in hands.values()
        ):
            raise RuleError(f"bad deal {hands}")
        rec.update(
            hands={str(p): h for p, h in hands.items()},
            game=self.r16(DEAL_NO),
            declarer=rec["dealer"] % 4 + 1,
            contract_keys=self.consumed,
            seed_play=self.ru32(RANDSEED),
        )
        self.consumed = []
        game = rec["game"]
        cur = {p: list(h) for p, h in hands.items()}
        trick: list[tuple[int, int]] = []
        tricks: list[Record] = []

        def on_hand(p: int) -> None:
            nonlocal trick
            card = self.r16(HANDS + 20 * p + 18)
            h = cur[p]
            if card not in h:
                raise RuleError(f"player {p} played {card} not in {h}")
            if trick:
                led = suit(trick[0][1])
                if suit(card) != led and any(suit(c) == led for c in h):
                    raise RuleError(f"player {p} did not follow suit: {card} {h}")
            elif game % 7 in NO_HEART_LEAD and suit(card) == 3 and any(suit(c) != 3 for c in h):
                raise RuleError(f"player {p} led a heart in game {game}: {card} {h}")
            i = h.index(card)
            if self.hand(p) != h[:i] + h[i + 1 :]:
                raise RuleError(f"hand of {p} not compacted")
            del h[i]
            trick.append((p, card))
            if len(trick) == 4:
                led = suit(trick[0][1])
                win = max((c, q) for q, c in trick if suit(c) == led)[1]
                tricks.append({"plays": trick, "winner": win})
                trick = []

        self.on_hand = on_hand
        self.call(PLAY_DEAL)
        self.on_hand = None
        if trick:
            raise RuleError(f"unfinished trick {trick}")
        if self.r16(ITEMS_LEFT) != 0:
            raise RuleError("items left at the end of the deal")
        for a, b in zip(tricks, tricks[1:], strict=False):
            if b["plays"][0][0] != a["winner"]:
                raise RuleError(f"{a} won but {b} led")
        rec.update(
            tricks=tricks,
            play_keys=self.consumed,
            deal_score=[self.r16(DEAL_SCORE + 2 * p) for p in range(1, 5)],
        )
        self.call(DEAL_SUMMARY)
        self.w16(DEALER, self.r16(DEALER) + 1)
        for p in range(1, 5):
            self.w16(DEAL_SCORE + 2 * p, 0)
        rec.update(totals=self.totals(), seed_end=self.ru32(RANDSEED))
        return rec

    def totals(self) -> list[int]:
        return [self.r16(TOTAL_SCORE + 2 * p) for p in range(1, 5)]
