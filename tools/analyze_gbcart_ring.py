#!/usr/bin/env python3
"""Parse the gbcart block-I/O ring and reconstruct what GB SRAM the game read.

Ring line (current format, after the thread/timing/MBC1 instrumentation):
  seq us=<us> thr=<OSThread*> P<port> <RD|WR> tpb<n> rom<n> rb<n> m<n> <acc> <bus> <b0> <flags>
  - bus   = resolved GB bus address
  - b0    = first byte of the 32-byte block at that address
  - SRAM  = bus in 0xA000..0xBFFF; file_offset = ram_bank*0x2000 + (bus-0xA000)
  - flags = P(pak)/C(cart)/R(ram) each present or '-'

Questions:
  1. Which SRAM banks did the game READ? (banks 2/3 read => box validation)
  2. Do the b0 samples match each .sav? (delivery check, 1 byte / 32)
  3. Coverage of the main-data checksum range 0xA598..0xB593 (bank 1).
  4. THREAD ATTRIBUTION: were all Transfer Pak ops single-threaded? (race check)
  5. FLAKE SIGNATURE: any SRAM-range read with cart/ram NOT enabled (flags
     missing C or R) -> the cart returns open-bus (0xFF/0x00), which corrupts
     the gen-1 main-data checksum -> red-X / bounce-to-menu. This is the
     suspected cause of the residual non-deterministic cart-validation flake.

Usage: analyze_gbcart_ring.py [ring.txt]   (default: build/build/gbcart_ring.txt)
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RING = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "build" / "gbcart_ring.txt")
REVA = ROOT / "gameboy" / "Pocket Monsters - Midori (Japan) (Rev A) (SGB Enhanced).sav"
REV1 = ROOT / "gameboy" / "Pocket Monsters - Midori (Japan) (Rev 1) (SGB Enhanced).sav"

# seq us=.. thr=.. P0 RD tpb2 rom33 rb1 m1 0xe598 0xa598 b0 PCR
LINE_RE = re.compile(
    r"^\s*(\d+)\s+us=(\d+)\s+thr=([0-9a-fA-F]+)\s+P(\d+)\s+(RD|WR)\s+tpb(\d+)\s+"
    r"rom(\d+)\s+rb(\d+)\s+m(\d+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+"
    r"([0-9a-fA-F]{2})\s+([PCR-]{3})")


def load(p):
    try:
        with open(p, "rb") as f:
            return f.read()
    except OSError:
        return b""


def main():
    reva, rev1 = load(REVA), load(REV1)
    rows = []  # dict per parsed row
    with open(RING, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = LINE_RE.match(ln)
            if not m:
                continue
            (seq, us, thr, port, kind, tpb, rom, rb, mbc1,
             acc, bus, b0, flags) = m.groups()
            rows.append(dict(
                seq=int(seq), us=int(us), thr=thr.lower(), port=int(port),
                kind=kind, tpb=int(tpb), rom=int(rom), rb=int(rb), mbc1=int(mbc1),
                acc=int(acc, 16), bus=int(bus, 16), b0=int(b0, 16), flags=flags))

    print(f"ring: {RING}")
    print(f"parsed rows: {len(rows)}")
    if not rows:
        print("  (no rows parsed — ring empty or format changed)")
        return

    # 4. Thread attribution
    thr_counts = {}
    for r in rows:
        thr_counts[r["thr"]] = thr_counts.get(r["thr"], 0) + 1
    print(f"threads issuing TPak ops: "
          + ", ".join(f"{t}={c}" for t, c in sorted(thr_counts.items(), key=lambda x: -x[1])))
    if len(thr_counts) > 1:
        print("  ** MULTIPLE THREADS — possible bank race; inspect interleaving **")

    sram_reads = [r for r in rows if r["kind"] == "RD" and 0xA000 <= r["bus"] < 0xC000]
    bank_rd = {}
    for r in sram_reads:
        bank_rd[r["rb"]] = bank_rd.get(r["rb"], 0) + 1
    print(f"\ntotal SRAM reads: {len(sram_reads)}")
    print(f"reads per ram_bank: {dict(sorted(bank_rd.items()))}")

    # 5. FLAKE SIGNATURE — SRAM read with cart or ram not enabled (open-bus read).
    bad = [r for r in sram_reads if "C" not in r["flags"] or "R" not in r["flags"]]
    print(f"\n[FLAKE SIGNATURE] SRAM-range reads with cart/ram NOT enabled: {len(bad)}")
    for r in bad[:30]:
        print(f"  seq{r['seq']} us={r['us']} rb{r['rb']} bus0x{r['bus']:04X} "
              f"b0=0x{r['b0']:02X} flags={r['flags']}  <- open-bus read")
    if len(bad) > 30:
        print(f"  ... +{len(bad)-30} more")

    # distinct (ram_bank, bus) coverage + last-b0
    by_bank = {}
    for r in sram_reads:
        by_bank.setdefault(r["rb"], {})[r["bus"]] = r["b0"]
    for rb in sorted(by_bank):
        offs = sorted(by_bank[rb])
        print(f"\nram_bank {rb}: {len(offs)} distinct bus addrs, "
              f"range 0x{offs[0]:04X}..0x{offs[-1]:04X}")

    # 2. delivery check: compare each sampled b0 to both saves at file offset
    def check(name, sav):
        if not sav:
            print(f"\n[delivery vs {name}] (save not found)")
            return
        mism, total = [], 0
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

    # 3. main-data checksum-range coverage (bank 1, GB 0xA598..0xB593)
    b1 = by_bank.get(1, {})
    in_range = [bus for bus in b1 if 0xA598 <= bus <= 0xB593]
    print(f"\n[main-data range 0xA598..0xB593] sampled blocks in bank1: {len(in_range)}")
    if in_range:
        print(f"  first 0x{min(in_range):04X} last 0x{max(in_range):04X}")


if __name__ == "__main__":
    main()
