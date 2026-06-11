#!/usr/bin/env python3
"""
scan_missing_funcs.py — find function entries Ghidra's auto-analysis missed.

The recompiler does not discover entry points; it relies on the Ghidra-derived
function list. Ghidra's flow analysis reaches functions via direct calls, but
misses functions only reached indirectly (thread entries passed to
osCreateThread, callbacks, jump-table cases) and whole regions it never
disassembled (e.g. the libnaudio synth block). Those show up at runtime as
`LOOKUP_FUNC` misses, or at recompile time as calls to undefined FUN_ symbols.

This scans every region of the section NOT covered by a known function for the
standard MIPS non-leaf prologue:

    addiu $sp, $sp, -N        (0x27BD with negative immediate)
    ... sw $ra, X($sp) ...    (0xAFBF...) within a few instructions, before any
                              `jr $ra` (0x03E00008)

Matches are emitted as function_adds.txt entries (size 0 => gen_symbols_toml.py
clamps each to the next entry). Leaf functions (no `sw $ra`) that are reached
only by a direct `jal` are intentionally not needed here — the recompiler
resolves those at compile time and a genuinely-missing one surfaces as an
undefined symbol or a recomp_unhandled_call, which is a separate, louder signal.

Usage:
    python tools/scan_missing_funcs.py --symbols symbols.toml --rom baserom.z64 \
        --out function_adds.txt
"""
import argparse, re, sys

ADDIU_SP_SP = 0x27BD0000   # addiu $sp,$sp,imm  (rs=rt=29)
SW_RA_SP    = 0xAFBF0000   # sw   $ra,imm($sp)  (base=29, rt=31)
JR_RA       = 0x03E00008   # jr   $ra


def parse_int(v):
    return v if isinstance(v, int) else int(str(v), 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--symbols", default="symbols.toml")
    ap.add_argument("--rom", default="baserom.z64")
    ap.add_argument("--out", default="function_adds.txt")
    ap.add_argument("--window", type=int, default=8,
                    help="instructions after a prologue to look for sw $ra")
    args = ap.parse_args()

    text = open(args.symbols, "r", encoding="utf-8").read()
    sec_rom  = parse_int(re.search(r"^rom\s*=\s*(\S+)",  text, re.M).group(1))
    sec_vram = parse_int(re.search(r"^vram\s*=\s*(\S+)", text, re.M).group(1))
    sec_size = parse_int(re.search(r"^size\s*=\s*(\S+)", text, re.M).group(1))
    sec_hi = sec_vram + sec_size

    funcs = [(parse_int(v), parse_int(s)) for v, s in
             re.findall(r"vram\s*=\s*(0x[0-9a-fA-F]+),\s*size\s*=\s*(0x[0-9a-fA-F]+)", text)]
    funcs.sort()
    known = {v for v, _ in funcs}

    # Covered byte intervals (vram space).
    covered = [(v, v + s) for v, s in funcs]

    rom = open(args.rom, "rb").read()

    def word(vram):
        off = sec_rom + (vram - sec_vram)
        return int.from_bytes(rom[off:off + 4], "big")

    def is_covered(vram):
        # Linear scan is fine for a one-shot tool; intervals are sorted.
        for lo, hi in covered:
            if lo <= vram < hi:
                return True
            if lo > vram:
                break
        return False

    # Pre-existing manual adds, so we don't duplicate.
    existing_adds = set()
    try:
        for line in open(args.out, "r", encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                existing_adds.add(parse_int(line.split()[0]))
    except FileNotFoundError:
        pass

    found = []
    vram = sec_vram
    while vram < sec_hi:
        if is_covered(vram) or vram in known or vram in existing_adds or vram in found:
            vram += 4
            continue
        w = word(vram)
        if (w & 0xFFFF0000) == ADDIU_SP_SP and (w & 0x8000):  # addiu sp,sp,-N
            # Confirm a sw $ra before any jr $ra within the window.
            ok = False
            for k in range(1, args.window + 1):
                wk = word(vram + 4 * k)
                if (wk & 0xFFFF0000) == SW_RA_SP:
                    ok = True
                    break
                if wk == JR_RA:
                    break
            if ok:
                found.append(vram)
                vram += 4
                continue
        vram += 4

    if not found:
        print("No missing function prologues found in uncovered gaps.")
        return

    with open(args.out, "a", encoding="utf-8") as out:
        out.write("\n# --- auto-discovered by tools/scan_missing_funcs.py "
                  "(uncovered-gap prologue scan) ---\n")
        for v in found:
            out.write(f"0x{v:08x}\n")
    print(f"Appended {len(found)} discovered function entr(ies) to {args.out}:")
    for v in found:
        print(f"  0x{v:08x}")


if __name__ == "__main__":
    main()
