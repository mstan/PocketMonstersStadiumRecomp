#!/usr/bin/env python3
"""
match_libultra.py — identify libultra (and libc/gcc) functions in this ROM by
signature-matching them against PokemonStadiumRecomp's pret ELF, which has
every such function named. Matched functions are renamed to their canonical
names in symbols.toml (via function_renames.txt), so the recompiler's HLE
lists (reimplemented_funcs / ignored_funcs / renamed_funcs in N64Recomp's
symbol_lists.cpp) skip them and N64ModernRuntime provides the implementation
— instead of recompiling the game's raw-MMIO libultra against a runtime that
doesn't emulate raw hardware registers.

Signature = the function's instruction words with address-bearing operands
masked out: j/jal 26-bit targets and the 16-bit immediates of lui/addiu/ori/
loads/stores (which carry absolute addresses / relocations) are zeroed, while
opcodes, register fields, R-type bodies, and PC-relative branch offsets are
kept. Two builds of the same libultra function then hash-match even though
their absolute addresses differ. We only accept a match when the reference
signature is UNIQUE among the reference set and the ROM function's length
matches — conservative, to avoid mis-naming.

Usage (run from the project root):
    python tools/match_libultra.py \
        --elf ../PokemonStadiumRecomp/disasm/build/pokestadium-us.elf \
        --symlists ../N64Recomp/src/symbol_lists.cpp \
        --functions functions.json --rom resident.bin \
        --rom-base 0x80000400 -o function_renames.txt
"""
import argparse, json, re, struct, hashlib

ADDR_BEARING = {8, 9, 13, 15,                 # addi, addiu, ori, lui
                32, 33, 34, 35, 36, 37, 38, 39,  # lb lh lwl lw lbu lhu lwr lwu
                40, 41, 42, 43, 44, 45, 46,      # sb sh swl sw sdl sdr swr
                49, 53, 55, 57, 61, 63}          # lwc1 ldc1 ldl? cop l/s


def mask_word(w):
    op = w >> 26
    if op in (2, 3):                 # j / jal — absolute 26-bit target
        return op << 26
    if op in ADDR_BEARING:           # immediate may carry an address/reloc
        return w & 0xFFFF0000
    return w                         # R-type, branches (relative), cop, etc.


def sig_of(words):
    h = hashlib.sha1()
    for w in words:
        h.update(struct.pack(">I", mask_word(w)))
    return (len(words), h.hexdigest())


def hle_names(symlists_path):
    src = open(symlists_path, encoding="utf-8", errors="replace").read()
    names = set()
    for nm in ("reimplemented_funcs", "ignored_funcs", "renamed_funcs"):
        m = re.search(nm + r".*?\{(.*?)\};", src, re.S)
        if m:
            names |= set(re.findall(r'"([^"]+)"', m.group(1)))
    return names


def ref_signatures(elf_path, names):
    from elftools.elf.elffile import ELFFile
    f = open(elf_path, "rb")
    e = ELFFile(f)
    st = e.get_section_by_name(".symtab")
    # Cache section data by index for byte extraction.
    sec_cache = {}
    def sec_data(idx):
        if idx not in sec_cache:
            s = e.get_section(idx)
            sec_cache[idx] = (s['sh_addr'], s.data())
        return sec_cache[idx]

    sig2name = {}
    collisions = set()
    n = 0
    for sym in st.iter_symbols():
        if sym['st_info']['type'] != 'STT_FUNC':
            continue
        if sym.name not in names or sym['st_size'] <= 0:
            continue
        shndx = sym['st_shndx']
        if not isinstance(shndx, int):
            continue
        sh_addr, data = sec_data(shndx)
        off = sym['st_value'] - sh_addr
        size = sym['st_size']
        if off < 0 or off + size > len(data):
            continue
        words = [struct.unpack(">I", data[off + i:off + i + 4])[0]
                 for i in range(0, size, 4)]
        sig = sig_of(words)
        if sig in sig2name and sig2name[sig] != sym.name:
            collisions.add(sig)
        else:
            sig2name[sig] = sym.name
        n += 1
    for c in collisions:
        sig2name.pop(c, None)        # ambiguous reference signature — drop
    print(f"  reference: {n} named funcs, {len(sig2name)} unique signatures "
          f"({len(collisions)} ambiguous dropped)")
    return sig2name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--symlists", required=True)
    ap.add_argument("--functions", default="functions.json")
    ap.add_argument("--rom", default="resident.bin")
    ap.add_argument("--rom-base", type=lambda v: int(v, 0), default=0x80000400)
    ap.add_argument("-o", "--out", default="function_renames.txt")
    args = ap.parse_args()

    names = hle_names(args.symlists)
    print(f"  HLE-relevant names: {len(names)}")
    sig2name = ref_signatures(args.elf, names)

    rom = open(args.rom, "rb").read()
    fns = json.load(open(args.functions))["functions"]
    renames = {}             # vram -> name
    used = {}                # name -> vram (first wins; warn on dupes)
    dup = 0
    for fn in fns:
        vram = fn["vram"]; size = fn["size"]
        off = vram - args.rom_base
        if off < 0 or off + size > len(rom) or size <= 0:
            continue
        words = [struct.unpack(">I", rom[off + i:off + i + 4])[0]
                 for i in range(0, size, 4)]
        name = sig2name.get(sig_of(words))
        if not name:
            continue
        if name in used:
            dup += 1
            continue          # same libultra name matched twice — skip extras
        used[name] = vram
        renames[vram] = name

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# function_renames.txt — libultra/libc functions identified by\n")
        f.write("# signature match vs PokemonStadiumRecomp's pret ELF. 'vram name'\n")
        f.write("# per line. Auto-generated by tools/match_libultra.py.\n")
        for vram in sorted(renames):
            f.write("0x%08x %s\n" % (vram, renames[vram]))

    print(f"  matched {len(renames)} ROM functions to canonical names "
          f"({dup} duplicate-name matches skipped)")
    print(f"  wrote {args.out}")


if __name__ == "__main__":
    main()
