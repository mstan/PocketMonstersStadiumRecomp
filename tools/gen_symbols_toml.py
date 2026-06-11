#!/usr/bin/env python3
"""
gen_symbols_toml.py — turn Ghidra auto-analysis output into an
N64Recomp symbols file (symbols.toml), for ROM-direct recompilation.

N64Recomp's symbols-file mode (Context::from_symbol_file) needs, per
executable section: rom offset, vram, size, and an explicit list of
functions {name, vram, size}. It does NOT discover function ENTRY points
itself (it deliberately skips j/jal targets), so we get the entry set
from Ghidra and emit it here. The engine reads each function's words
straight from the ROM using vram/size; sizes therefore matter.

Input: a JSON file (from tools/ghidra_export.py) shaped as
    { "functions": [ { "name": str, "vram": int, "size": int }, ... ] }
(vram/size may be ints or hex strings like "0x80000400".)

We keep only functions inside the declared section's [vram, vram+size)
range, sort by vram, drop zero/negative sizes, and CLAMP each function's
size so it never overruns the next function's entry — that yields a clean
non-overlapping partition (gaps between functions are just data the
recompiler ignores), which is what the engine expects.

Usage:
    python gen_symbols_toml.py functions.json -o symbols.toml \
        --section-name text --rom 0x1000 --vram 0x80000400 --size 0x7FC00
"""
import argparse, json, sys


def parse_int(v):
    if isinstance(v, int):
        return v
    return int(str(v), 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("functions_json")
    ap.add_argument("-o", "--out", default="symbols.toml")
    ap.add_argument("--section-name", default="text")
    ap.add_argument("--rom", type=parse_int, default=0x1000,
                    help="ROM offset of section start")
    ap.add_argument("--vram", type=parse_int, default=0x80000400,
                    help="VRAM (link) address of section start")
    ap.add_argument("--size", type=parse_int, default=0x7FC00,
                    help="Section byte size (covers code+data up to BSS)")
    ap.add_argument("--entry-vram", type=parse_int, default=0x80000400,
                    help="Expected entrypoint vram (must resolve to rom 0x1000)")
    ap.add_argument("--drops", default="function_drops.txt",
                    help="File of vram addresses (one per line, # comments) to "
                         "EXCLUDE — false-positive 'functions' that are really "
                         "data, found during recompile bring-up.")
    ap.add_argument("--merges", default="function_merges.txt",
                    help="File of vram addresses to remove so the PREDECESSOR "
                         "function absorbs them — repairs Ghidra over-splits "
                         "(auto-derived by tools/derive_boundary_merges.py).")
    ap.add_argument("--sizes", default="function_sizes.txt",
                    help="File of 'vram size' lines forcing a MINIMUM byte size "
                         "for functions Ghidra under-sized "
                         "(auto-derived by tools/derive_boundary_merges.py).")
    ap.add_argument("--renames", default="function_renames.txt",
                    help="File of 'vram name' lines renaming functions to "
                         "canonical libultra/libc names (from "
                         "tools/match_libultra.py) so the recompiler's HLE "
                         "lists skip them and the runtime provides them.")
    ap.add_argument("--adds", default="function_adds.txt",
                    help="File of 'vram [size] [name]' lines ADDING function "
                         "entries Ghidra's auto-analysis missed — typically "
                         "indirect/computed-call targets (thread entries, "
                         "callbacks) that only surface as runtime LOOKUP_FUNC "
                         "misses during bring-up. size/name optional (size 0 -> "
                         "clamp to next entry; name -> FUN_<vram>). Takes "
                         "priority over any same-vram Ghidra entry.")
    args = ap.parse_args()

    def load_addr_file(path, label):
        s = set()
        try:
            with open(path, "r", encoding="utf-8") as df:
                for line in df:
                    line = line.split("#", 1)[0].strip()
                    if line:
                        s.add(parse_int(line))
            print(f"  loaded {len(s)} {label} from {path}")
        except FileNotFoundError:
            pass
        return s

    # Drops (data false-positives) and merges (over-split repairs) both just
    # remove the entry from the function list; the difference is intent. A
    # merged entry's bytes get absorbed by the adjacent predecessor's clamp.
    drops = load_addr_file(args.drops, "drop(s)")
    drops |= load_addr_file(args.merges, "merge(s)")

    size_overrides = {}
    try:
        with open(args.sizes, "r", encoding="utf-8") as sf:
            for line in sf:
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                v, s = line.split()
                size_overrides[parse_int(v)] = parse_int(s)
        print(f"  loaded {len(size_overrides)} size-override(s) from {args.sizes}")
    except FileNotFoundError:
        pass

    renames = {}
    try:
        with open(args.renames, "r", encoding="utf-8") as rf:
            for line in rf:
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                v, nm = line.split()
                renames[parse_int(v)] = nm
        print(f"  loaded {len(renames)} rename(s) from {args.renames}")
    except FileNotFoundError:
        pass

    # Manual additions (vram [size] [name]) for entries Ghidra missed.
    adds = {}
    try:
        with open(args.adds, "r", encoding="utf-8") as af:
            for line in af:
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                parts = line.split()
                v = parse_int(parts[0])
                sz = parse_int(parts[1]) if len(parts) >= 2 else 0
                nm = parts[2] if len(parts) >= 3 else f"FUN_{v:08x}"
                adds[v] = {"name": nm, "vram": v, "size": sz}
        print(f"  loaded {len(adds)} add(s) from {args.adds}")
    except FileNotFoundError:
        pass

    sec_lo = args.vram
    sec_hi = args.vram + args.size

    with open(args.functions_json, "r", encoding="utf-8") as f:
        data = json.load(f)
    raw = data["functions"] if isinstance(data, dict) else data

    funcs = []
    for fn in raw:
        vram = parse_int(fn["vram"])
        size = parse_int(fn.get("size", 0))
        name = fn["name"]
        if vram < sec_lo or vram >= sec_hi:
            continue            # outside this section (overlay/data noise)
        if vram in drops:
            continue            # explicit false-positive exclusion
        if vram & 3:
            print(f"  WARN skipping non-word-aligned func {name} @ {vram:#x}",
                  file=sys.stderr)
            continue
        funcs.append({"name": name, "vram": vram, "size": size})

    # Inject manual adds, taking priority over any same-vram Ghidra entry, and
    # only those that fall inside this section's range and aren't dropped.
    if adds:
        funcs = [f for f in funcs if f["vram"] not in adds]
        for v, fn in adds.items():
            if v < sec_lo or v >= sec_hi or v in drops:
                continue
            if v & 3:
                print(f"  WARN skipping non-word-aligned add @ {v:#x}", file=sys.stderr)
                continue
            funcs.append(dict(fn))

    if not funcs:
        sys.exit("No functions fell within the section range — wrong "
                 "base/range, or analysis not run?")

    # Sort, dedup by vram (keep first), clamp sizes to next entry.
    funcs.sort(key=lambda x: x["vram"])
    deduped = []
    for fn in funcs:
        if deduped and deduped[-1]["vram"] == fn["vram"]:
            continue
        deduped.append(fn)
    funcs = deduped

    for i, fn in enumerate(funcs):
        next_vram = funcs[i + 1]["vram"] if i + 1 < len(funcs) else sec_hi
        max_size = next_vram - fn["vram"]
        size = fn["size"]
        ov = size_overrides.get(fn["vram"], 0)
        if ov > size:
            size = ov                       # extend under-sized function
        if size <= 0 or size > max_size:
            size = max_size                 # clamp / fill to next entry
        size &= ~3                          # word-align the size down
        if size == 0:
            size = 4
        fn["size"] = size

    has_entry = any(fn["vram"] == args.entry_vram for fn in funcs)
    if not has_entry:
        print(f"  WARN no function at entry vram {args.entry_vram:#x} — "
              f"the recompiler will reject the entrypoint.", file=sys.stderr)

    with open(args.out, "w", encoding="utf-8") as out:
        out.write("# symbols.toml — generated by tools/gen_symbols_toml.py\n")
        out.write("# Do not hand-edit; re-run the generator.\n\n")
        out.write("[[section]]\n")
        out.write(f'name = "{args.section_name}"\n')
        out.write(f"rom = {args.rom:#x}\n")
        out.write(f"vram = {args.vram:#x}\n")
        out.write(f"size = {args.size:#x}\n")
        out.write("functions = [\n")
        nrenamed = 0
        for fn in funcs:
            name = renames.get(fn["vram"], fn["name"])
            if name != fn["name"]:
                nrenamed += 1
            out.write(f'    {{ name = "{name}", '
                      f'vram = {fn["vram"]:#x}, size = {fn["size"]:#x} }},\n')
        out.write("]\n")

    print(f"Wrote {args.out}: {len(funcs)} functions in section "
          f"'{args.section_name}' [{sec_lo:#x}, {sec_hi:#x}) "
          f"(rom {args.rom:#x}); {nrenamed} renamed to canonical names.")


if __name__ == "__main__":
    main()
