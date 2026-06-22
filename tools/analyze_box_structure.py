#!/usr/bin/env python3
"""Inspect Gen-1 JP PC-box structure + checksums in a .sav (banks 2 & 3).

JP geometry (4 boxes/bank, box size 0x566): bank-relative box starts
0x000, 0x566, 0xACC, 0x1032; box region 0x000..0x1597; all-boxes checksum
byte at 0x1598; per-box checksum bytes at 0x1599..0x159C.
Each box: byte0 = count (0..30). CalcCheckSum = ~(sum) & 0xFF.
"""
import sys

def ones(b, a, z):  # inclusive
    s = 0
    for i in range(a, z + 1):
        s += b[i]
    return (~s) & 0xFF

def main(path):
    with open(path, "rb") as f:
        b = f.read()
    print(f"{path}\n")
    BOX = 0x566
    for bank, base in (("bank2", 0x4000), ("bank3", 0x6000)):
        starts = [base + i * BOX for i in range(4)]
        counts = [b[s] for s in starts]
        allbox_off = base + 0x1598
        perbox_off = [base + 0x1599 + i for i in range(4)]
        allbox_calc = ones(b, base, allbox_off - 1)          # sum region 0..0x1597
        allbox_stored = b[allbox_off]
        print(f"{bank} (base 0x{base:04X}):")
        print(f"  box counts @starts: {counts}  (valid if each 0..30)")
        print(f"  all-boxes chk @0x{allbox_off:04X}: calc=0x{allbox_calc:02X} "
              f"stored=0x{allbox_stored:02X} "
              f"{'OK' if allbox_calc==allbox_stored else 'MISMATCH'}")
        for i in range(4):
            s = starts[i]
            calc = ones(b, s, s + BOX - 1)
            stored = b[perbox_off[i]]
            print(f"    box{i} @0x{s:04X} count={b[s]:3d}  perbox chk "
                  f"calc=0x{calc:02X} stored@0x{perbox_off[i]:04X}=0x{stored:02X} "
                  f"{'OK' if calc==stored else 'MISMATCH'}")
        # dump the 8 trailing bytes (checksum area)
        tail = b[allbox_off:allbox_off + 8]
        print(f"  trailing 8 @0x{allbox_off:04X}: {' '.join(f'{x:02X}' for x in tail)}")
        print()

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev 1) (SGB Enhanced).sav")
