#!/usr/bin/env python3
"""
pms_fontrender.py — render the font_slotN.i8 sheets dumped by the runtime's
"fontdump" command into viewable grayscale PNGs (tiled glyph grids).

Reads fontdump.log (next to the exe, i.e. build/) for each slot's tile w/h,
then lays each w*h glyph tile into a 16-column grid. Dependency-free PNG
writer (stdlib zlib only) so the result is directly viewable.

Usage:
  python tools/pms_fontrender.py [--dir build] [--cols 16]
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
import binascii
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def write_png_gray(path: Path, w: int, h: int, pixels: bytes) -> None:
    """Minimal 8-bit grayscale PNG."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", binascii.crc32(tag + data) & 0xFFFFFFFF))
    # filter byte 0 per scanline
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(pixels[y * w:(y + 1) * w])
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)  # 8-bit grayscale
    idat = zlib.compress(bytes(raw), 9)
    path.write_bytes(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))


def render_sheet(i8: Path, tw: int, th: int, cols: int, out: Path) -> int:
    data = i8.read_bytes()
    tile = tw * th
    if tile == 0:
        return 0
    nglyphs = len(data) // tile
    if nglyphs == 0:
        return 0
    rows = (nglyphs + cols - 1) // cols
    # 1px separator between tiles so glyph boundaries are visible.
    gw = cols * (tw + 1) + 1
    gh = rows * (th + 1) + 1
    img = bytearray([64]) * (gw * gh)  # mid-gray background
    for g in range(nglyphs):
        gx = (g % cols) * (tw + 1) + 1
        gy = (g // cols) * (th + 1) + 1
        base = g * tile
        for y in range(th):
            row = data[base + y * tw: base + (y + 1) * tw]
            di = (gy + y) * gw + gx
            img[di:di + len(row)] = row
    write_png_gray(out, gw, gh, bytes(img))
    return nglyphs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=str(ROOT / "build"))
    ap.add_argument("--cols", type=int, default=16)
    args = ap.parse_args()
    d = Path(args.dir)

    log = d / "fontdump.log"
    if not log.exists():
        print(f"no fontdump.log in {d} — run the 'fontdump' debug command first")
        return 1
    text = log.read_text(encoding="utf-8", errors="replace")
    print(text)

    # lines: "  0     12   16   15   0x80201234  61440 bytes -> font_slot0.i8 (tile 16x15)"
    pat = re.compile(r"->\s+(font_slot\d+\.i8)\s+\(tile\s+(\d+)x(\d+)\)")
    found = 0
    for m in pat.finditer(text):
        fname, tw, th = m.group(1), int(m.group(2)), int(m.group(3))
        i8 = d / fname
        if not i8.exists():
            print(f"  (missing {fname})")
            continue
        out = d / (fname.replace(".i8", ".png"))
        n = render_sheet(i8, tw, th, args.cols, out)
        print(f"  rendered {fname} ({tw}x{th}, {n} glyphs) -> {out}")
        found += 1
    if found == 0:
        print("no font sheets dumped (font state may be uninitialized)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
