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

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16, Uc
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_IP,
                               UC_X86_REG_SP, UC_X86_REG_SS)

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '..', 'original', 'KING2.EXE')

CS = 0x1000
DS = 0x1d2e
CRT, GRAPH, SYSTEM = 0x1ba3, 0x1871, 0x1c05
STACK_SEG = 0x1e29                 # SS from the EXE header
STACK_TOP = 0x7d00
SENTINEL = (0x9000, 0x0000)        # far return address that ends a call (unused memory)

# Routines replaced by a bare return (bytes popped). Each one only draws, plays sound or waits;
# none writes a variable the game logic reads (checked in the decompiled callees).
STUBS = {
    (CS, 0x70e4): ('put_sprite', 'ret', 0x0e),
    (CS, 0x0295): ('draw_text', 'retf', 0x12),
    (CS, 0x057b): ('fill_rect_bytes', 'retf', 8),
    (CS, 0x0615): ('copy_rect_bytes', 'retf', 8),
    (CS, 0x06a9): ('animate_card', 'retf', 0x0c),
    (CS, 0x1f98): ('draw_box', 'retf', 0x0a),
    (CS, 0x567b): ('draw_counter', 'retf', 6),
    (CS, 0x575c): ('draw_counters', 'retf', 0),
    (CS, 0x67da): ('flash_contract', 'retf', 0),
    (CS, 0x066e): ('flush_input', 'retf', 0),
    (CS, 0x3eb9): ('wait_space_or_click', 'retf', 2),
    (CRT, 0x029e): ('Delay', 'retf', 2),
    (CRT, 0x02c6): ('Sound', 'retf', 2),
    (CRT, 0x02f3): ('NoSound', 'retf', 0),
    (GRAPH, 0x0d05): ('SetPalette', 'retf', 4),
}

# names (KING2 addresses)
DEAL, PLAY_DEAL, DEAL_SUMMARY = 0x685c, 0x6e1d, 0x3f36
DRAW_HAND, KEYPRESSED, READKEY, QUIT = 0x159d, 0x02fa, 0x030c, 0x0000

# DGROUP variables (KING2)
RANDSEED = 0x0418
GAMES_PLAYED = 0x0500              # + 30p + 2g
GAMES_LEFT = 0x051c                # + 30p
DEAL_SCORE, TOTAL_SCORE = 0x064a, 0x0652   # + 2p
HANDS = 0x06fa                     # + 20p: count, cards[1..8], table card [9]
DEALER, DEAL_NO = 0x07b0, 0x07be
MOUSE = 0x0596


class Quit(Exception):
    pass


class King:
    def __init__(self):
        raw = open(EXE, 'rb').read()
        hdr = struct.unpack('<H', raw[8:10])[0] * 16
        img = bytearray(raw[hdr:])
        nrel = struct.unpack('<H', raw[6:8])[0]
        rtab = struct.unpack('<H', raw[0x18:0x1a])[0]
        for k in range(nrel):
            off, seg = struct.unpack('<HH', raw[rtab + 4 * k: rtab + 4 * k + 4])
            p = seg * 16 + off
            v = struct.unpack('<H', img[p:p + 2])[0]
            img[p:p + 2] = struct.pack('<H', (v + CS) & 0xffff)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(CS * 16, bytes(img))
        self.keys = []             # script: a char (str of length 1) or None = one idle poll
        self.key_source = None     # called with no arguments to refill an empty script
        self.max_insns = 20_000_000_000   # per call; only a guard against endless loops
        self.on_hand = None        # callback(player) when draw_hand runs
        for (seg, off), (_name, kind, n) in STUBS.items():
            code = (b'\xc2' if kind == 'ret' else b'\xca') + struct.pack('<H', n) if n else \
                (b'\xc3' if kind == 'ret' else b'\xcb')
            mu.mem_write(seg * 16 + off, code)
        self.pyfuncs = {
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
    def rb(self, off, n=1): return bytes(self.mu.mem_read(DS * 16 + off, n))
    def r16(self, off): return struct.unpack('<h', self.rb(off, 2))[0]
    def ru32(self, off): return struct.unpack('<I', self.rb(off, 4))[0]
    def wb(self, off, data): self.mu.mem_write(DS * 16 + off, bytes(data))
    def w16(self, off, v): self.wb(off, struct.pack('<H', v & 0xffff))
    def w32(self, off, v): self.wb(off, struct.pack('<I', v & 0xffffffff))

    def hand(self, p):
        n = self.r16(HANDS + 20 * p)
        return [self.r16(HANDS + 20 * p + 2 * i) for i in range(1, n + 1)]

    # ---- replaced routines ----
    def keypressed(self, args):
        if not self.keys and self.key_source:
            self.keys += self.key_source()
        if self.keys and self.keys[0] is None:
            self.keys.pop(0)
            return 0
        if not self.keys:
            raise RuntimeError('key script exhausted')
        return 1

    def readkey(self, args):
        if not self.keys or self.keys[0] is None:
            raise RuntimeError('ReadKey without a scripted key')
        return ord(self.keys.pop(0))

    def draw_hand(self, args):
        if self.on_hand: self.on_hand(args[0])
        return 0

    def quit(self, args):
        raise Quit()

    def _intr(self, mu, intno, _):
        ip = mu.reg_read(UC_X86_REG_IP)
        cs = mu.reg_read(UC_X86_REG_CS)
        raise RuntimeError(f'unhandled int {intno:02x} at {cs:04x}:{ip:04x}')

    def _io(self, mu, port, size, value=None, _=None):
        cs = mu.reg_read(UC_X86_REG_CS)
        ip = mu.reg_read(UC_X86_REG_IP)
        raise RuntimeError(f'port I/O {port:x} at {cs:04x}:{ip:04x}: a drawing routine is not stubbed')

    def _py(self, mu, addr, size, _):
        seg = mu.reg_read(UC_X86_REG_CS)
        fn, nbytes = self.pyfuncs[(seg, addr - seg * 16)]
        sp = mu.reg_read(UC_X86_REG_SP)
        ss = mu.reg_read(UC_X86_REG_SS)
        def rd(o): return struct.unpack('<H', bytes(mu.mem_read(ss * 16 + ((sp + o) & 0xffff), 2)))[0]
        ip, cs = rd(0), rd(2)
        args = [rd(4 + nbytes - 2 - 2 * k) for k in range(nbytes // 2)]    # source order
        try:
            ax = fn(args)
        except BaseException as e:
            self.error = e
            mu.emu_stop()
            return
        mu.reg_write(UC_X86_REG_AX, (ax or 0) & 0xffff)
        mu.reg_write(UC_X86_REG_SP, (sp + 4 + nbytes) & 0xffff)
        mu.reg_write(UC_X86_REG_CS, cs)
        mu.reg_write(UC_X86_REG_IP, ip)

    # ---- calling ----
    def call(self, off, *args, seg=CS):
        """Far call seg:off with Pascal arguments (source order)."""
        mu = self.mu
        mu.reg_write(UC_X86_REG_DS, DS)
        mu.reg_write(UC_X86_REG_ES, DS)
        mu.reg_write(UC_X86_REG_SS, STACK_SEG)
        sp = STACK_TOP
        def push(v):
            nonlocal sp
            sp -= 2
            mu.mem_write(STACK_SEG * 16 + sp, struct.pack('<H', v & 0xffff))
        for a in args: push(a)
        push(SENTINEL[0]); push(SENTINEL[1])
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_BP, 0)
        mu.reg_write(UC_X86_REG_CS, seg)
        self.error = None
        mu.emu_start(seg * 16 + off, SENTINEL[0] * 16 + SENTINEL[1], count=self.max_insns)
        if self.error: raise self.error
        here = mu.reg_read(UC_X86_REG_CS) * 16 + mu.reg_read(UC_X86_REG_IP)
        if here != SENTINEL[0] * 16 + SENTINEL[1]:
            raise RuntimeError(f'call {seg:04x}:{off:04x} stopped at {here:05x}')
        return mu.reg_read(UC_X86_REG_AX)


class Game(King):
    """A game as KING2's main plays it after the login: 56 deals."""

    def setup(self, seed):
        self.w32(RANDSEED, seed)
        for p in range(1, 5):
            for g in range(14): self.w16(GAMES_PLAYED + 30 * p + 2 * g, 0)
            self.w16(GAMES_LEFT + 30 * p, 14)
            self.w16(DEAL_SCORE + 2 * p, 0)
            self.w16(TOTAL_SCORE + 2 * p, 0)
        self.w16(DEALER, 0)

    def games_left(self):
        return sum(self.r16(GAMES_LEFT + 30 * p) for p in range(1, 5))

    def play_one_deal(self):
        self.call(DEAL)
        self.call(PLAY_DEAL)
        self.call(DEAL_SUMMARY)
        self.w16(DEALER, self.r16(DEALER) + 1)
        for p in range(1, 5): self.w16(DEAL_SCORE + 2 * p, 0)

    def totals(self):
        return [self.r16(TOTAL_SCORE + 2 * p) for p in range(1, 5)]
