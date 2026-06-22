#!/usr/bin/env python3
"""Parse the gbcart block-I/O ring and reconstruct what GB SRAM the game read.

Ring line: seq port kind tpb rom_bank ram_bank acc bus b0 flags
  - bus  = resolved GB bus address
  - b0   = first byte of the 32-byte block at that address
  - SRAM = bus in 0xA000..0xBFFF; file_offset = ram_bank*0x2000 + (bus-0xA000)

Questions:
  1. Which SRAM banks did the game READ? (banks 2/3 read => box validation)
  2. Do the b0 samples match each .sav? (delivery check, 1 byte / 32)
  3. Coverage of the main-data checksum range 0xA598..0xB593 (bank 1).
"""
import re, sys

RING = r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\build\build\gbcart_ring.txt"
REVA = r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev A) (SGB Enhanced).sav"
REV1 = r"F:\Projects\n64recomp\PocketMonstersStadiumRecomp\gameboy\Pocket Monsters - Midori (Japan) (Rev 1) (SGB Enhanced).sav"

def load(p):
    with open(p, "rb") as f:
        return f.read()

def main():
    reva, rev1 = load(REVA), load(REV1)
    line_re = re.compile(
        r"^\s*(\d+)\s+P\d+\s+(RD|WR)\s+tpb\d+\s+rom(\d+)\s+rb(\d+)\s+"
        r"(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+([0-9a-f]{2})\s+(\S+)")
    sram_reads = []   # (seq, ram_bank, bus, b0)
    bank_rd = {}      # ram_bank -> count of SRAM reads
    with open(RING, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = line_re.match(ln)
            if not m:
                continue
            seq, kind, rom_bank, ram_bank, acc, bus, b0, flags = m.groups()
            seq = int(seq); ram_bank = int(ram_bank)
            bus = int(bus, 16); b0 = int(b0, 16)
            if 0xA000 <= bus < 0xC000 and kind == "RD":
                sram_reads.append((seq, ram_bank, bus, b0))
                bank_rd[ram_bank] = bank_rd.get(ram_bank, 0) + 1

    print(f"total SRAM reads: {len(sram_reads)}")
    print(f"reads per ram_bank: {dict(sorted(bank_rd.items()))}")

    # distinct (ram_bank, bus) and file-offset coverage
    by_bank = {}
    for seq, rb, bus, b0 in sram_reads:
        by_bank.setdefault(rb, {})[bus] = b0  # last b0 wins
    for rb in sorted(by_bank):
        offs = sorted(by_bank[rb])
        print(f"\nram_bank {rb}: {len(offs)} distinct bus addrs, "
              f"range 0x{offs[0]:04X}..0x{offs[-1]:04X}")

    # delivery check: compare each sampled b0 to both saves at file offset
    def check(name, sav):
        mism = []
        total = 0
        for rb, d in by_bank.items():
            for bus, b0 in d.items():
                fo = rb * 0x2000 + (bus - 0xA000)
                if fo >= len(sav):
                    continue
                total += 1
                if sav[fo] != b0:
                    mism.append((rb, bus, fo, b0, sav[fo]))
        print(f"\n[delivery vs {name}] samples={total} mismatches={len(mism)}")
        for rb, bus, fo, got, exp in mism[:40]:
            print(f"  rb{rb} bus0x{bus:04X} file0x{fo:04X}: ring=0x{got:02X} sav=0x{exp:02X}")
        if len(mism) > 40:
            print(f"  ... +{len(mism)-40} more")
    check("RevA", reva)
    check("Rev1", rev1)

    # main-data checksum-range coverage (bank 1, GB 0xA598..0xB593)
    b1 = by_bank.get(1, {})
    in_range = [bus for bus in b1 if 0xA598 <= bus <= 0xB593]
    print(f"\n[main-data range 0xA598..0xB593] sampled blocks in bank1: {len(in_range)}")
    if in_range:
        print(f"  first 0x{min(in_range):04X} last 0x{max(in_range):04X}")

if __name__ == "__main__":
    main()
