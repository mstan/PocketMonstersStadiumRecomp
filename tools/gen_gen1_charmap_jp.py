#!/usr/bin/env python3
"""Generate src/main/gen1_charmap_jp.h from pret/pokered-jp's charmap.asm.

The Japanese Gen-1 games store player/box names in an in-game text encoding
(kana + digits) that differs from the international Latin set. This script
parses the authoritative disassembly charmap into a byte -> UTF-8 table the
launcher uses to decode the trainer name from a Japanese .sav.

Usage:
    python tools/gen_gen1_charmap_jp.py path/to/charmap.asm
(defaults to ./charmap.asm). Source:
    https://raw.githubusercontent.com/luckytyphlosion/pokered-jp/master/charmap.asm
"""
import re
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "charmap.asm")
OUT = os.path.normpath(os.path.join(HERE, "..", "src", "main", "gen1_charmap_jp.h"))

pat = re.compile(r'charmap\s+"(.*?)",\s*\$([0-9A-Fa-f]{1,2})')
table = {}
with open(SRC, encoding="utf-8") as fh:
    for line in fh:
        m = pat.search(line)
        if not m:
            continue
        glyph, value = m.group(1), int(m.group(2), 16)
        if len(glyph) != 1:        # skip multi-char macros / control names
            continue
        if ord(glyph) < 0x20:      # skip control codepoints
            continue
        table[value] = glyph

table.pop(0x50, None)              # 0x50 = terminator, handled by the decoder
table[0x7F] = " "                  # 0x7F = space


def utf8_escape(text):
    return "".join("{0}x{1:02X}".format(chr(92), b) for b in text.encode("utf-8"))


lines = []
lines.append("// Auto-generated from pret/pokered-jp charmap.asm - do not edit by hand.")
lines.append("// Maps a Japanese Gen-1 in-game text byte to its UTF-8 glyph (\"\" = none).")
lines.append("// Regenerate via tools/gen_gen1_charmap_jp.py.")
lines.append("#pragma once")
lines.append("")
lines.append("static const char* const kGen1JpCharmap[256] = {")
for base in range(0, 256, 8):
    cells = []
    for b in range(base, base + 8):
        glyph = table.get(b, "")
        cells.append('"{0}"'.format(utf8_escape(glyph) if glyph else ""))
    lines.append("    " + ", ".join(cells) + ",")
lines.append("};")
lines.append("")

with open(OUT, "w", encoding="ascii") as fh:
    fh.write("\n".join(lines))

print("wrote", OUT, "with", len(table), "mapped bytes")
