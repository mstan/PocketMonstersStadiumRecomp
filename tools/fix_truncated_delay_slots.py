#!/usr/bin/env python3
"""Offline fixer for functions whose symbols-file size cuts off the final
branch/jump's delay slot.

The N64Recomp truncation guard (process_delay_slot) makes these a hard
generation error; fixing them one-per-recompile is slow. This scans every
function in symbols.toml, decodes its LAST instruction word from the ROM, and
if that word is a branch/jump (i.e. its delay slot lies outside the function),
appends a size override (+4) to function_sizes.txt.

Usage: python tools/fix_truncated_delay_slots.py [baserom.z64] [symbols.toml]
Then re-run gen_symbols_toml.py and N64Recomp.
"""
import re, struct, sys

def is_delay_slot_instr(w):
    op = (w >> 26) & 0x3F
    if op in (2, 3):                      # j, jal
        return True
    if op in (4, 5, 6, 7):                # beq, bne, blez, bgtz
        return True
    if op in (0x14, 0x15, 0x16, 0x17):    # beql, bnel, blezl, bgtzl
        return True
    if op == 1:                           # REGIMM: bltz/bgez/bltzl/bgezl(+al)
        rt = (w >> 16) & 0x1F
        return rt in (0, 1, 2, 3, 16, 17, 18, 19)
    if op == 0:                           # SPECIAL: jr, jalr
        funct = w & 0x3F
        return funct in (8, 9)
    if op == 0x11:                        # COP1: BC1 (bc1f/bc1t/bc1fl/bc1tl)
        rs = (w >> 21) & 0x1F
        return rs == 8
    return False

def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else 'baserom.z64'
    syms_path = sys.argv[2] if len(sys.argv) > 2 else 'symbols.toml'
    rom = open(rom_path, 'rb').read()

    SECTION_VRAM = 0x80000400
    SECTION_ROM = 0x1000

    funcs = []
    for line in open(syms_path, encoding='utf-8'):
        m = re.search(r'name = "([^"]+)", vram = (0x[0-9a-fA-F]+), size = (0x[0-9a-fA-F]+)', line)
        if m:
            funcs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))

    fixes = []
    for name, vram, size in funcs:
        if size < 4:
            continue
        last_va = vram + size - 4
        off = last_va - SECTION_VRAM + SECTION_ROM
        if off + 4 > len(rom):
            continue
        w = struct.unpack_from('>I', rom, off)[0]
        if is_delay_slot_instr(w):
            fixes.append((vram, size + 4, name, last_va, w))

    if not fixes:
        print('no truncated functions found')
        return 0
    with open('function_sizes.txt', 'a', encoding='utf-8') as f:
        f.write('# auto-extended by tools/fix_truncated_delay_slots.py: function ends with a\n')
        f.write('# branch/jump whose delay slot is outside the recorded size (+4 each).\n')
        for vram, size, name, last_va, w in fixes:
            f.write(f'0x{vram:08x} 0x{size:x}\n')
    print(f'extended {len(fixes)} function(s):')
    for vram, size, name, last_va, w in fixes[:10]:
        print(f'  {name}: last word 0x{w:08X} @ 0x{last_va:08X} -> size 0x{size:X}')
    if len(fixes) > 10:
        print(f'  ... and {len(fixes)-10} more')
    return 0

if __name__ == '__main__':
    sys.exit(main())
