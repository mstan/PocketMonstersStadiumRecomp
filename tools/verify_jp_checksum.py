#!/usr/bin/env python3
"""Compute PMS-J's EXACT GB-Tower main-data checksum on a Gen-1 .sav.

PMS-J resident verifier (Ghidra 0x80084ce0):
  s = 0; for i in 0x2598..0x3593 (inclusive): s += sav[i]
  chk = (s & 0xFF) ^ 0xFF          # one's complement
  pass if chk == sav[0x3594]
  plus a head-scan: 0x50 must appear in 0x2598..0x259D (name terminator)

Also tests the research-agent's computed JP offset (0x35D6 sum end / 0x35D7 byte)
for comparison, and reports zero/FF density past 0x3594 to judge whether a
"consistent" offset there is genuine or a flat-region coincidence.
"""
import sys

def ones_complement_sum(b, start, end_inclusive):
    s = 0
    for i in range(start, end_inclusive + 1):
        s += b[i]
    return (s & 0xFF) ^ 0xFF

def main(path):
    with open(path, "rb") as f:
        b = f.read()
    print(f"file: {path}  size: 0x{len(b):X}\n")

    # --- PMS-J's exact check (Ghidra) ---
    chk_game = ones_complement_sum(b, 0x2598, 0x3593)
    stored_game = b[0x3594]
    print("[PMS-J game check]  sum 0x2598..0x3593, ones-complement, vs byte 0x3594")
    print(f"  calc=0x{chk_game:02X}  stored=0x{stored_game:02X}  "
          f"{'PASS' if chk_game == stored_game else 'FAIL'}")

    head = b[0x2598:0x259E]
    print(f"  head-scan 0x2598..0x259D = {' '.join(f'{x:02X}' for x in head)}  "
          f"-> 0x50 present: {0x50 in head}")

    # --- research-agent computed offset ---
    chk_r = ones_complement_sum(b, 0x2598, 0x35D6)
    stored_r = b[0x35D7]
    print(f"\n[research-computed]  sum 0x2598..0x35D6, vs byte 0x35D7")
    print(f"  calc=0x{chk_r:02X}  stored=0x{stored_r:02X}  "
          f"{'PASS' if chk_r == stored_r else 'FAIL'}")

    # --- box-bank (banks 2/3) checksums (JP: 4 boxes/bank, box size 0x566,
    #     region 0x000..0x1597 bank-relative, all-boxes checksum at 0x1598) ---
    print("\n[box banks 2/3 — JP all-boxes checksum @ bank-rel 0x1598]")
    for bank, base in (("bank2", 0x4000), ("bank3", 0x6000)):
        region_end = base + 0x1597
        chk = ones_complement_sum(b, base, region_end)
        ck_off = base + 0x1598
        stored = b[ck_off]
        allff = all(x == 0xFF for x in b[base:base + 0x1598])
        print(f"  {bank}: sum 0x{base:04X}..0x{region_end:04X} -> calc=0x{chk:02X}  "
              f"stored@0x{ck_off:04X}=0x{stored:02X}  "
              f"{'PASS' if chk == stored else 'FAIL'}  "
              f"box-region-all-0xFF={allff}")
        # scan this bank for ANY self-consistent all-boxes checksum offset
        hits = [k for k in range(base + 0x100, base + 0x2000)
                if ones_complement_sum(b, base, k - 1) == b[k] and not
                all(x == 0xFF for x in b[base:k])]
        print(f"         non-trivial consistent offsets in {bank}: "
              f"{[hex(h) for h in hits[:8]]}{' ...' if len(hits) > 8 else ''}")

    # --- flat-region check past the checksum byte ---
    tail = b[0x3595:0x3F94]
    zeros = sum(1 for x in tail if x == 0x00)
    ffs = sum(1 for x in tail if x == 0xFF)
    print(f"\n[region 0x3595..0x3F93]  len={len(tail)}  zeros={zeros}  "
          f"0xFF={ffs}  other={len(tail)-zeros-ffs}")

    def hexdump(start, end):
        for row in range(start, end, 16):
            chunk = b[row:row + 16]
            print(f"  0x{row:04X}: {' '.join(f'{x:02X}' for x in chunk)}")
    print("\n[hexdump 0x3560..0x35E0] (around both candidate checksum bytes)")
    hexdump(0x3560, 0x35E0)

    # --- JP-correct header fields (sMainData @ 0x259E, name len 6) ---
    # sMainData order: PokedexOwned(19) PokedexSeen(19) bag... money badges TID
    # We give a coarse readout; exact sub-offsets need JP wram, but enough to
    # judge "real progress".
    smain = 0x259E
    dex_owned = b[smain:smain + 19]
    owned_cnt = sum(bin(x).count("1") for x in dex_owned)
    dex_seen = b[smain + 19:smain + 38]
    seen_cnt = sum(bin(x).count("1") for x in dex_seen)
    print(f"\n[JP header @sMainData=0x{smain:X}]")
    print(f"  name @0x2598: {' '.join(f'{x:02X}' for x in b[0x2598:0x259E])}")
    print(f"  pokedex owned popcount: {owned_cnt}   seen popcount: {seen_cnt}")
    print("\n[hexdump 0x2598..0x2620] (JP header)")
    hexdump(0x2598, 0x2620)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev A) (SGB Enhanced).sav")
