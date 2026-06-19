#!/usr/bin/env python3
"""Build assets/NotoSansJP-Subset.ttf — a small, redistributable (OFL) Japanese
font for the launcher's kana trainer names + JP cart labels.

Subsets Noto Sans JP (OFL, (c) Google) down to ASCII + the kana blocks + the
handful of kanji the launcher actually shows, keeping the TTF tiny.

Usage:
    pip install brotli fonttools
    curl -L -o noto-sans-jp.woff2 \
      https://cdn.jsdelivr.net/npm/@fontsource/noto-sans-jp@5/files/noto-sans-jp-japanese-400-normal.woff2
    python tools/build_jp_font.py noto-sans-jp.woff2
The OFL license ships alongside the font as assets/NotoSansJP-OFL.txt.
"""
import os
import sys
from fontTools import subset
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "noto-sans-jp.woff2")
OUT = os.path.normpath(os.path.join(HERE, "..", "assets", "NotoSansJP-Subset.ttf"))

unicodes = set()
unicodes |= set(range(0x20, 0x7F))       # ASCII (digits, latin, punctuation)
unicodes |= set(range(0x3000, 0x3100))   # CJK punct + hiragana + katakana (names)
unicodes |= set(range(0xFF00, 0xFFA0))   # half/fullwidth forms
for cp in (0x8D64, 0x7DD1, 0x9EC4, 0x9752):  # 赤 緑 黄 青 (cart labels)
    unicodes.add(cp)

font = TTFont(SRC)  # reading woff2 needs the 'brotli' package
opts = subset.Options()
opts.name_IDs = ["*"]
opts.notdef_outline = True
ss = subset.Subsetter(options=opts)
ss.populate(unicodes=sorted(unicodes))
ss.subset(font)
font.flavor = None  # emit a plain TTF (FreeType-friendly, no brotli at runtime)
font.save(OUT)

out = TTFont(OUT)
cmap = out.getBestCmap()
required = [0x3042, 0x30A2, 0x30FC, 0x8D64, 0x7DD1, 0x9752, 0x41, 0x30]
missing = [hex(cp) for cp in required if cp not in cmap]
print("wrote", OUT, "glyphs:", len(cmap), "missing:", missing or "none")
