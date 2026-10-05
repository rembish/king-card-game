"""Decoders for the data files of KING (1993): KING.LIB sprites, KING.FNT fonts, KING.OVL players.

    python3 re/tools/king.py lib [out.png]     contact sheet of all sprites (needs Pillow)
    python3 re/tools/king.py fnt [out.png]     the three fonts (needs Pillow)
    python3 re/tools/king.py ovl               the player registry (passwords not shown)

Formats are as read by KING.EXE; see re/NOTES.md for the routines.
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(HERE, "..", "..", "original")

# EGA default palette as the game sets it (6-bit rgbRGB values), after the SetPalette calls in
# init_graphics (1000:04ba): 4 = 0x14, 5 = 0x27, 6 = 0x2e. Colour 13 changes per half of the
# game (0x3b "don't take", 0x3c "take"); 0x3b is used here.
EGA_REGS = [0, 1, 2, 3, 0x14, 0x27, 0x2E, 7, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3B, 0x3E, 0x3F]


def ega_rgb(v: int) -> tuple[int, int, int]:
    r = ((v >> 2) & 1) * 2 + ((v >> 5) & 1)
    g = ((v >> 1) & 1) * 2 + ((v >> 4) & 1)
    b = (v & 1) * 2 + ((v >> 3) & 1)
    return (r * 85, g * 85, b * 85)


Sprite = tuple[int, int, list[list[int]]]


def read_lib(path: str | None = None) -> list[Sprite | None]:
    """KING.LIB: 128-byte header (count, then size of each sprite in 128-byte records), then the
    sprites back to back. A sprite: u16 width, u16 height, u16 unused, then per row a u16 byte
    length and RLE runs: b >= 0x80 -> (b - 0x80) copies of the next byte, else b literal bytes.
    One byte per pixel (EGA colour). Entries of size 0 are unused (card ranks 2..6)."""
    d = open(path or os.path.join(ORIG, "KING.LIB"), "rb").read()
    n = d[0]
    off = 128
    out: list[Sprite | None] = []
    for k in range(n):
        size = d[1 + k] * 128
        s = d[off : off + size]
        off += size
        if not size:
            out.append(None)
            continue
        w, h, _ = struct.unpack("<3H", s[:6])
        p = 6
        rows = []
        for _y in range(h):
            (length,) = struct.unpack("<H", s[p : p + 2])
            q, end = p + 2, p + 2 + length
            row: list[int] = []
            while q < end:
                c = s[q]
                q += 1
                if c & 0x80:
                    row += [s[q]] * (c - 0x80)
                    q += 1
                else:
                    row += list(s[q : q + c])
                    q += c
            rows.append(row)
            p = end
        out.append((w, h, rows))
    return out


def read_fnt(path: str | None = None) -> list[tuple[int, bytes]]:
    """KING.FNT: three 256-glyph, 8-pixel-wide bitmap fonts back to back: 6, 8 and 14 rows."""
    d = open(path or os.path.join(ORIG, "KING.FNT"), "rb").read()
    fonts, off = [], 0
    for rows in (6, 8, 14):
        fonts.append((rows, d[off : off + 256 * rows]))
        off += 256 * rows
    return fonts


def read_ovl(path: str | None = None) -> list[tuple[str, int, int]]:
    """KING.OVL: 32-byte records: string[12] name (13 bytes), string[4] password (5 bytes),
    8 unused bytes, i32 balance, u16 games played. Only the characters of the two strings are
    obfuscated (XOR 0x1A, 1000:0877); length bytes and numbers are plain."""
    d = open(path or os.path.join(ORIG, "KING.OVL"), "rb").read()
    out = []
    for i in range(0, len(d) - 31, 32):
        r = d[i : i + 32]
        name = bytes(b ^ 0x1A for b in r[1 : 1 + min(r[0], 12)]).decode("cp866", "replace")
        balance, games = struct.unpack("<iH", r[26:32])
        out.append((name, balance, games))
    return out


def main() -> None:
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "ovl":
        for name, bal, games in read_ovl():
            print(f"{name:12} {bal:8} {games:5}")
        return
    from PIL import Image

    pal = [ega_rgb(v) for v in EGA_REGS]
    if cmd == "lib":
        sprites = read_lib()
        im = Image.new("RGB", (1100, 340), (40, 40, 40))
        x = y = rowh = 0
        for sp in sprites:
            if sp is None:
                continue
            w, h, rows = sp
            if x + w > im.width:
                x, y, rowh = 0, y + rowh + 4, 0
            for yy, row in enumerate(rows):
                for xx, c in enumerate(row[:w]):
                    im.putpixel((x + xx, y + yy), pal[c & 15])
            x += w + 4
            rowh = max(rowh, h)
    elif cmd == "fnt":
        fonts = read_fnt()
        im = Image.new("RGB", (32 * 9, sum(8 * (r + 1) for r, _ in fonts)), (0, 0, 0))
        y0 = 0
        for rows, data in fonts:
            for g in range(256):
                gx, gy = (g % 32) * 9, y0 + (g // 32) * (rows + 1)
                for yy in range(rows):
                    bits = data[g * rows + yy]
                    for xx in range(8):
                        if bits & (0x80 >> xx):
                            im.putpixel((gx + xx, gy + yy), (255, 255, 255))
            y0 += 8 * (rows + 1)
    else:
        sys.exit(__doc__)
    out = sys.argv[2] if len(sys.argv) > 2 else f"{cmd}.png"
    im.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
