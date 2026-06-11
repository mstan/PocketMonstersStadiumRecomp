"""Build the synthetic rspboot+aspMain image for Pocket Monsters Stadium.

RSPRecomp sees one contiguous IMEM image. Real rspboot first occupies
IMEM[0..0x80), then DMAs aspMain into IMEM[0x80..0x1080). The task's
ucode_boot_size is 0xD0, but rspboot itself sets SP_MEM_ADDR to 0x1080,
so bytes at IMEM[0x80..] are overwritten by aspMain before they execute.
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


EXPECTED_MD5 = "c46e087d966a35095df96799b0b4ffae"

RSPBOOT_ROM_OFFSET = 0x5D8C0
RSPBOOT_SIZE = 0x80
ASPMAIN_ROM_OFFSET = 0x5ED20
ASPMAIN_SIZE = 0x1000


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=str(here / "baserom.z64"))
    ap.add_argument("--out", default=str(here / "aspmain_combined.bin"))
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
    aspmain = rom[ASPMAIN_ROM_OFFSET : ASPMAIN_ROM_OFFSET + ASPMAIN_SIZE]
    combined = rspboot + aspmain
    if len(combined) != RSPBOOT_SIZE + ASPMAIN_SIZE:
        print("ERROR: ROM slices were shorter than expected", file=sys.stderr)
        return 1

    out_path = Path(args.out)
    out_path.write_bytes(combined)
    print(f"Wrote {out_path} ({len(combined)} bytes)")
    print(f"  rspboot: ROM[0x{RSPBOOT_ROM_OFFSET:X}..0x{RSPBOOT_ROM_OFFSET + RSPBOOT_SIZE:X})")
    print(f"  aspMain: ROM[0x{ASPMAIN_ROM_OFFSET:X}..0x{ASPMAIN_ROM_OFFSET + ASPMAIN_SIZE:X})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
