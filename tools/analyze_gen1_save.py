#!/usr/bin/env python3
"""Analyze a Gen-1 (Red/Blue/Green/Yellow) .sav for its internal save checksums.

We need to know whether the save's main-data complement checksum is
self-consistent, and at WHICH offset, so we can tell apart:
  (a) PMS-J validates over the wrong (international) range  -> code bug
  (b) the save genuinely has a bad Gen-1 checksum           -> save problem

Gen-1 main-data checksum = ~(sum of bytes over the main-data block) & 0xFF,
stored in the byte immediately after the block. International (pokered):
  block 0x2598..0x3522, checksum byte at 0x3523.
The JP layout starts the player name / main data at the same 0xA598 (file
0x2598) but the block END differs because box/party/name field sizes differ,
so the checksum offset differs. We scan to find it empirically.
"""
import sys

def main(path):
    with open(path, "rb") as f:
        b = f.read()
    print(f"file: {path}")
    print(f"size: {len(b)} bytes (0x{len(b):X})")
    if len(b) < 0x8000:
        print("WARNING: smaller than a 32KB Gen-1 SRAM image")

    START = 0x2598  # sMainData / sPlayerName (file offset), same in INT and JP

    def cksum(start, end_inclusive):
        s = 0
        for i in range(start, end_inclusive + 1):
            s += b[i]
        return (~s) & 0xFF

    # International expectation
    int_calc = cksum(START, 0x3522)
    int_stored = b[0x3523]
    print(f"\n[international] block 0x2598..0x3522, checksum byte @0x3523")
    print(f"  calc=0x{int_calc:02X}  stored=0x{int_stored:02X}  "
          f"{'MATCH' if int_calc == int_stored else 'MISMATCH'}")

    # Scan: for each candidate checksum-byte offset k, treat the block as
    # START..k-1 and see if stored[k] == ~sum & 0xFF. Report all consistent k.
    print(f"\n[scan] start fixed at 0x{START:X}; candidate checksum offsets where "
          f"stored == ~sum(start..k-1)&0xFF:")
    hits = []
    for k in range(START + 0x10, 0x4000):
        if cksum(START, k - 1) == b[k]:
            hits.append(k)
    # Many spurious 1-in-256 hits; show them but flag the well-known regions.
    for k in hits:
        note = ""
        if k == 0x3523:
            note = "  <- international sMainDataCheckSum"
        print(f"  0x{k:04X}{note}")
    print(f"  ({len(hits)} consistent offsets total in 0x{START:X}..0x4000)")

    # Player name + trainer id (same field positions INT/JP per ui_seam.cpp)
    name = b[0x2598:0x25A3]
    print(f"\nplayer-name bytes @0x2598: {' '.join(f'{x:02X}' for x in name)}")
    tid = (b[0x2605] << 8) | b[0x2606]
    print(f"trainer id @0x2605 (BE): {tid} (0x{tid:04X})")

    # Progress fields (INTERNATIONAL offsets; JP may shift later fields, but the
    # early header up to TID is shared per ui_seam.cpp). Use these only as a
    # rough "is there real progress?" signal.
    dex_owned = b[0x25A3:0x25B6]   # 19 bytes bitfield, 151 dex bits
    owned_cnt = sum(bin(x).count("1") for x in dex_owned)
    badges = b[0x2602]
    money = b[0x25F3:0x25F6]       # 3-byte BCD
    money_str = "".join(f"{x:02X}" for x in money)
    print(f"\n[INT-offset progress signals]")
    print(f"  pokedex owned (popcount @0x25A3..0x25B5): {owned_cnt}")
    print(f"  badges byte @0x2602: 0x{badges:02X}  (popcount {bin(badges).count('1')})")
    print(f"  money BCD @0x25F3: {money_str}")
    # party count + species list at INT sMainData party (0x2F2C)
    pc = b[0x2F2C]
    species = b[0x2F2D:0x2F2D + 7]
    print(f"  party count @0x2F2C: {pc}  species: {' '.join(f'{x:02X}' for x in species)}")

    def hexdump(start, end):
        for row in range(start, end, 16):
            chunk = b[row:row + 16]
            hx = " ".join(f"{x:02X}" for x in chunk)
            print(f"  0x{row:04X}: {hx}")
    print("\n[hexdump 0x2598..0x2660] (name / dex / header)")
    hexdump(0x2598, 0x2660)
    print("\n[hexdump 0x2F20..0x2F60] (INT party region)")
    hexdump(0x2F20, 0x2F60)

    # Bank map of the file (each bank 0x2000): show first 16 bytes of each
    print("\nbank previews (file 0x2000 each):")
    for bank in range(4):
        off = bank * 0x2000
        prev = b[off:off + 16]
        allff = all(x == 0xFF for x in b[off:off + 0x2000])
        all00 = all(x == 0x00 for x in b[off:off + 0x2000])
        tag = " [all 0xFF]" if allff else (" [all 0x00]" if all00 else "")
        print(f"  bank{bank} @0x{off:04X}: {' '.join(f'{x:02X}' for x in prev)}{tag}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev A) (SGB Enhanced).sav")
