#!/usr/bin/env python3
"""Recompute the Gen-1 JP PC-box (SRAM banks 2 & 3) checksums in a .sav.

Many save-editor saves write real box data but leave stale checksum bytes
(here: 0x8E filler), which PMS-J's GB-Tower validation rejects. This rewrites
the all-boxes + per-box checksums to match the actual box data, using the same
one's-complement-of-sum algorithm the game uses for main data.

JP geometry: 4 boxes/bank, box size 0x566; box region bank-rel 0x000..0x1597;
all-boxes checksum @0x1598; per-box checksums @0x1599..0x159C.
Main-data checksum (bank 1) is left untouched (already valid).
Writes <name> (repaired).sav next to the source.
"""
import sys, os

BOX = 0x566

def ones(b, a, z):  # inclusive one's-complement of 8-bit sum
    s = 0
    for i in range(a, z + 1):
        s += b[i]
    return (~s) & 0xFF

def main(src):
    with open(src, "rb") as f:
        b = bytearray(f.read())
    assert len(b) >= 0x8000, "not a 32KB Gen-1 SRAM image"
    changes = []
    for base in (0x4000, 0x6000):  # bank2, bank3
        # per-box checksums first
        for i in range(4):
            s = base + i * BOX
            calc = ones(b, s, s + BOX - 1)
            off = base + 0x1599 + i
            if b[off] != calc:
                changes.append((off, b[off], calc))
            b[off] = calc
        # all-boxes checksum over the 4-box region
        allcalc = ones(b, base, base + 0x1598 - 1)
        aoff = base + 0x1598
        if b[aoff] != allcalc:
            changes.append((aoff, b[aoff], allcalc))
        b[aoff] = allcalc

    root, ext = os.path.splitext(src)
    dst = f"{root} (repaired){ext}"
    with open(dst, "wb") as f:
        f.write(b)
    print(f"source : {src}")
    print(f"output : {dst}")
    print(f"box-checksum bytes rewritten: {len(changes)}")
    for off, old, new in changes:
        print(f"  file 0x{off:04X}: 0x{old:02X} -> 0x{new:02X}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev 1) (SGB Enhanced).sav")
