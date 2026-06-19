"""Build the synthetic rspboot+njpgdsp image for Pocket Monsters Stadium.

RSPRecomp sees one contiguous IMEM image. Real rspboot first occupies
IMEM[0..0x80), then DMAs njpgdsp into IMEM[0x80..0x1080) (SP_MEM_ADDR=0x1080)
and jr's into it. Recompiling rspboot+njpgdsp together gives the ucode the
DMA-register state real rspboot leaves behind.

njpgdsp is PMS-J's OWN JPEG ucode (ROM 0x5F980). The prior rsp/njpgdspMain.cpp
was generated from Pokemon Stadium US's njpgdsp and reused for PMS-J; the
binaries differ (IMEM 0x0/0x18), so the decode read the wrong DMEM constants
and produced all-zero (green) output. rspboot is the standard libultra boot
stub, byte-identical to aspMain's (ROM 0x5D8C0).
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


EXPECTED_MD5 = "c46e087d966a35095df96799b0b4ffae"

RSPBOOT_ROM_OFFSET = 0x5D8C0
RSPBOOT_SIZE = 0x80
NJPGDSP_ROM_OFFSET = 0x5F980
NJPGDSP_SIZE = 0x1000


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=str(here / "baserom.z64"))
    ap.add_argument("--out", default=str(here / "njpgdsp_combined.bin"))
    args = ap.parse_args(argv)

    rom_path = Path(args.rom)
    rom = rom_path.read_bytes()
    md5 = hashlib.md5(rom).hexdigest()
    if md5 != EXPECTED_MD5:
        print(
            f"ERROR: ROM md5 mismatch for {rom_path}. "
            f"Expected {EXPECTED_MD5}, got {md5}",
            file=sys.stderr,
        )
        return 1

    rspboot = rom[RSPBOOT_ROM_OFFSET : RSPBOOT_ROM_OFFSET + RSPBOOT_SIZE]
    njpgdsp = rom[NJPGDSP_ROM_OFFSET : NJPGDSP_ROM_OFFSET + NJPGDSP_SIZE]
    combined = rspboot + njpgdsp
    if len(combined) != RSPBOOT_SIZE + NJPGDSP_SIZE:
        print("ERROR: ROM slices were shorter than expected", file=sys.stderr)
        return 1

    out_path = Path(args.out)
    out_path.write_bytes(combined)
    print(f"Wrote {out_path} ({len(combined)} bytes)")
    print(f"  rspboot: ROM[0x{RSPBOOT_ROM_OFFSET:X}..0x{RSPBOOT_ROM_OFFSET + RSPBOOT_SIZE:X})")
    print(f"  njpgdsp: ROM[0x{NJPGDSP_ROM_OFFSET:X}..0x{NJPGDSP_ROM_OFFSET + NJPGDSP_SIZE:X})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
