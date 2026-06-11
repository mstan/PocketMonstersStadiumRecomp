#!/usr/bin/env python3
"""
mips_find_functions.py — general-purpose MIPS R4300i function-entry seeder.

A recursive-descent control-flow discoverer for N64 (MIPS III / R4300i) code,
modeled on segagenesisrecomp's recompiler/src/function_finder.c (68K) but with
our own MIPS decoder — no Ghidra dependency. The intent is a standalone front
end that seeds the recompiler's function set; Ghidra is only a cross-check
baseline today and is meant to drop out of the loop entirely later.

Approach (mirrors the 68K finder):
  * Seed from trusted entry points: the ROM entrypoint, every function Ghidra
    already knows (when supplied), and the start of each uncovered gap that
    decodes as a real prologue.
  * Walk each seed's control flow instruction-by-instruction. A *validator*
    (legal-opcode tables) stops a path the moment it decodes something that
    isn't a legal R4300i instruction — that's data, not code.
  * CALL targets become functions: `jal` (direct) and the branch-and-link
    REGIMM forms (`bltzal`/`bgezal`/...). `jalr` is an indirect call we can't
    resolve statically.
  * BRANCH / `j` targets are explored on the SAME path (more call sites to
    find) but are NOT themselves registered as functions — exactly the 68K
    finder's BSR/JSR-vs-BRA/Bcc distinction.
  * `jr $ra` / `eret` terminate a path; `jr $reg` (register, non-$ra) is an
    indirect dispatch (jump table) we log as unresolved for follow-up.
  * Delay slots are accounted for (the instruction after a transfer executes).

Output: function entries Ghidra didn't have are appended to function_adds.txt
(with sizes computed by walking each function to its terminator, so trailing
data is excluded — better than gen_symbols_toml.py's clamp-to-next). Unresolved
indirect-dispatch sites are dumped to a log.

Usage:
    python tools/mips_find_functions.py --rom baserom.z64 \
        --rom-base 0x1000 --vram 0x80000400 --size 0x7FC00 \
        --entry 0x80000400 --known functions.json --out function_adds.txt
"""
import argparse, json, sys

# ---- MIPS R4300i legality tables (validator) -------------------------------
# Primary opcodes (bits 31..26) that are legal on the R4300i.
VALID_OP = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12,                 # COP0/1/2
    0x14, 0x15, 0x16, 0x17,           # beql/bnel/blezl/bgtzl
    0x18, 0x19, 0x1A, 0x1B,           # daddi/daddiu/ldl/ldr
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3C, 0x3D, 0x3E, 0x3F,
}
# SPECIAL (op 0) funct codes that are legal.
VALID_SPECIAL = {
    0x00, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x09, 0x0C, 0x0D, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x36,
    0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F,
}
# REGIMM (op 1) rt codes that are legal.
VALID_REGIMM = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E,
                0x10, 0x11, 0x12, 0x13}
REGIMM_LINK   = {0x10, 0x11, 0x12, 0x13}          # bltzal/bgezal/bltzall/bgezall
REGIMM_BRANCH = {0x00, 0x01, 0x02, 0x03} | REGIMM_LINK


def parse_int(v):
    return v if isinstance(v, int) else int(str(v), 0)


class Decoder:
    """Classifies one 32-bit big-endian MIPS word for control-flow walking."""
    def __init__(self):
        pass

    def is_legal(self, w):
        op = (w >> 26) & 0x3F
        if op not in VALID_OP:
            return False
        if op == 0x00:
            return (w & 0x3F) in VALID_SPECIAL
        if op == 0x01:
            return ((w >> 16) & 0x1F) in VALID_REGIMM
        return True

    def classify(self, w, pc):
        """Return (kind, target). kind in:
           nop, call, branch, jump, ret, indirect_call, indirect_jump, other, invalid
           'call'/'branch'/'jump' carry an absolute target; others target=None.
           'branch' includes a flag via kind 'ubranch' for the unconditional b idiom."""
        if w == 0:
            return ("nop", None)
        if not self.is_legal(w):
            return ("invalid", None)
        op = (w >> 26) & 0x3F
        if op == 0x03:                                   # jal
            return ("call", (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2))
        if op == 0x02:                                   # j
            return ("jump", (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2))
        if op == 0x00:
            funct = w & 0x3F
            if funct == 0x08:                            # jr
                rs = (w >> 21) & 0x1F
                return ("ret", None) if rs == 31 else ("indirect_jump", None)
            if funct == 0x09:                            # jalr
                return ("indirect_call", None)
            return ("other", None)
        if op == 0x01:                                   # REGIMM
            rt = (w >> 16) & 0x1F
            simm = w & 0xFFFF
            if simm & 0x8000:
                simm -= 0x10000
            tgt = pc + 4 + (simm << 2)
            if rt in REGIMM_LINK:
                return ("call", tgt)                     # branch-and-link = call
            if rt in REGIMM_BRANCH:
                return ("branch", tgt)
            return ("other", None)
        if op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):  # beq/bne/.../*l
            simm = w & 0xFFFF
            if simm & 0x8000:
                simm -= 0x10000
            tgt = pc + 4 + (simm << 2)
            rs = (w >> 21) & 0x1F
            rt = (w >> 16) & 0x1F
            # `b off` == beq $0,$0 (and `bnel`-no such; only beq) is unconditional.
            if op == 0x04 and rs == 0 and rt == 0:
                return ("ubranch", tgt)
            return ("branch", tgt)
        if op == 0x11:                                   # COP1
            rs = (w >> 21) & 0x1F
            if rs == 0x08:                               # BC1 (bc1f/bc1t/bc1fl/bc1tl)
                simm = w & 0xFFFF
                if simm & 0x8000:
                    simm -= 0x10000
                return ("branch", pc + 4 + (simm << 2))
            return ("other", None)
        if op == 0x10:                                   # COP0
            # eret = COP0 with funct 0x18 (CO=1). Treat as a path terminator.
            if (w & 0x3F) == 0x18 and (w >> 25) & 1:
                return ("ret", None)
            return ("other", None)
        return ("other", None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="baserom.z64")
    ap.add_argument("--rom-base", type=parse_int, default=0x1000,
                    help="file offset of section start")
    ap.add_argument("--vram", type=parse_int, default=0x80000400)
    ap.add_argument("--size", type=parse_int, default=0x7FC00)
    ap.add_argument("--entry", type=parse_int, default=0x80000400)
    ap.add_argument("--known", default="functions.json",
                    help="Ghidra functions.json (seeds + cross-check); optional")
    ap.add_argument("--out", default="function_adds.txt")
    ap.add_argument("--unresolved-log", default="generated/unresolved_dispatch.log")
    ap.add_argument("--gap-seed", action="store_true", default=True,
                    help="also seed the start of each uncovered gap if it "
                         "decodes as a prologue (catches indirect-only entries "
                         "like thread funcs that no jal points at)")
    ap.add_argument("--report-only", action="store_true",
                    help="print stats and the new entries but do not write files")
    args = ap.parse_args()

    sec_lo = args.vram
    sec_hi = args.vram + args.size
    rom = open(args.rom, "rb").read()
    dec = Decoder()

    def in_sec(v):
        return sec_lo <= v < sec_hi

    def word(v):
        off = args.rom_base + (v - sec_lo)
        return int.from_bytes(rom[off:off + 4], "big")

    # Known functions (seeds + cross-check baseline). We keep both the entry set
    # AND Ghidra's [vram, vram+size) bodies — the latter is the authoritative
    # data-gate: a discovered target that lands STRICTLY INSIDE a known body is
    # either data misdecoded as a `jal` target or a spurious mid-function split,
    # and registering it would truncate the enclosing function. Reject those
    # (the segagenesis `is_known_code` gate, using Ghidra as the baseline).
    import bisect
    known = set()
    known_bodies = []           # list of (lo, hi) sorted by lo
    if args.known:
        try:
            data = json.load(open(args.known, "r", encoding="utf-8"))
            raw = data["functions"] if isinstance(data, dict) else data
            for fn in raw:
                v = parse_int(fn["vram"])
                s = parse_int(fn.get("size", 0))
                if in_sec(v):
                    known.add(v)
                    if s > 0:
                        known_bodies.append((v, v + s))
        except FileNotFoundError:
            print(f"  (no {args.known}; running without Ghidra baseline)")
    known_bodies.sort()
    _body_los = [lo for lo, _ in known_bodies]

    def inside_known_body(v):
        # True if v is strictly inside some known [lo, hi) (v != lo).
        i = bisect.bisect_right(_body_los, v) - 1
        if i < 0:
            return False
        lo, hi = known_bodies[i]
        return lo < v < hi

    # ---- recursive-descent discovery ---------------------------------------
    functions = set()           # call-target + seed entries (the output domain)
    explored = set()            # code addresses already walked (path heads)
    reached = set()             # EVERY instruction address visited (any path)
    work = []                   # exploration stack of code addresses
    unresolved = []             # (pc) of jr $reg indirect dispatch sites

    def add_function(v):
        if in_sec(v) and (v & 3) == 0 and v not in functions:
            # data-gate: reject targets strictly inside a known Ghidra body
            # (data-as-code / spurious split) and words that aren't legal insns.
            if inside_known_body(v):
                return
            if dec.is_legal(word(v)):
                functions.add(v)
                push(v)

    def push(v):
        if in_sec(v) and (v & 3) == 0 and v not in explored:
            explored.add(v)
            work.append(v)

    # Seed: entrypoint + all known Ghidra functions.
    add_function(args.entry)
    for v in known:
        add_function(v)

    def walk(start):
        pc = start
        while in_sec(pc):
            w = word(pc)
            kind, tgt = dec.classify(w, pc)
            if kind == "invalid":
                return
            reached.add(pc)        # this address is reachable code, not a gap
            if kind == "call":
                if tgt is not None:
                    add_function(tgt)
                pc += 4
                continue
            if kind in ("branch",):
                if tgt is not None:
                    push(tgt)
                pc += 4
                continue
            if kind == "ubranch":
                if tgt is not None:
                    push(tgt)
                # unconditional: delay slot executes, then path leaves
                return
            if kind == "jump":
                if tgt is not None:
                    push(tgt)          # explore (local jump or tailcall) — not a function
                return
            if kind in ("ret", "indirect_jump"):
                if kind == "indirect_jump":
                    unresolved.append(pc)
                return
            # indirect_call (jalr), nop, other → fall through
            pc += 4

    while work:
        walk(work.pop())

    # ---- gap-seed pass: catch indirect-only entries (thread funcs etc.) -----
    # Functions reached only via osCreateThread/callback pointers are never the
    # target of a jal, so the CFG walk above won't find them. Seed the start of
    # each uncovered gap (between sorted discovered+known funcs) when it decodes
    # as a non-leaf prologue (addiu sp,sp,-N then sw ra), then walk from it.
    if args.gap_seed:
        roots = sorted(functions | known)
        # Build covered set by walking each root's extent (cheap: to next root).
        gap_seeds = []
        for i, f in enumerate(roots):
            nxt = roots[i + 1] if i + 1 < len(roots) else sec_hi
            # scan the gap after this function's plausible end for a prologue
            v = f
            # find this function's terminating region by linear scan to nxt
            # (we only need the FIRST prologue in any uncovered run)
        # Scan every word-aligned addr that is neither a known function entry
        # NOR already-reached code (reached during a CFG walk => it belongs to
        # an existing function's body, e.g. a mid-function stack adjustment that
        # happens to look like a prologue — seeding it would falsely split the
        # enclosing function and truncate it).
        v = sec_lo
        while v < sec_hi:
            if v in functions or v in reached or inside_known_body(v):
                v += 4
                continue
            w = word(v)
            if (w & 0xFFFF0000) == 0x27BD0000 and (w & 0x8000):   # addiu sp,sp,-N
                ok = False
                for k in range(1, 9):
                    wk = word(v + 4 * k)
                    if (wk & 0xFFFF0000) == 0xAFBF0000:           # sw ra,X(sp)
                        ok = True
                        break
                    if wk == 0x03E00008:                          # jr ra
                        break
                if ok and dec.is_legal(w):
                    gap_seeds.append(v)
                    add_function(v)
            v += 4
        # walk newly-seeded prologues to discover their callees too
        while work:
            walk(work.pop())
        print(f"  gap-seed: {len(gap_seeds)} prologue-only entr(ies) seeded")

    # ---- size each discovered function (walk to terminator, exclude data) ---
    all_funcs = sorted(functions | known)

    def func_size(entry, limit):
        # BFS within [entry, limit) following fall-through + branch targets,
        # stopping at terminators; size = furthest reachable instr + 4.
        seen = set()
        stack = [entry]
        maxend = entry + 4
        while stack:
            pc = stack.pop()
            while entry <= pc < limit:
                if pc in seen:
                    break
                seen.add(pc)
                w = word(pc)
                kind, tgt = dec.classify(w, pc)
                if kind == "invalid":
                    break
                if pc + 4 > maxend:
                    maxend = pc + 4
                if kind in ("ret",):
                    maxend = max(maxend, pc + 8)   # include delay slot
                    break
                if kind == "ubranch" or kind == "jump":
                    if tgt is not None and entry <= tgt < limit:
                        stack.append(tgt)
                    maxend = max(maxend, pc + 8)
                    break
                if kind == "branch" and tgt is not None and entry <= tgt < limit:
                    stack.append(tgt)
                if kind == "indirect_jump":
                    maxend = max(maxend, pc + 8)
                    break
                pc += 4
        return min(maxend - entry, limit - entry)

    # Compute sizes only for NEW functions (not known to Ghidra).
    new_funcs = sorted(functions - known)
    idx = {f: i for i, f in enumerate(all_funcs)}
    sized = []
    for f in new_funcs:
        i = idx[f]
        nxt = all_funcs[i + 1] if i + 1 < len(all_funcs) else sec_hi
        sized.append((f, func_size(f, nxt)))

    # ---- report + cross-check ----------------------------------------------
    rediscovered = len(functions & known)
    print(f"[mips-find] discovered {len(functions)} function entries "
          f"({rediscovered}/{len(known)} of Ghidra's re-found; "
          f"{len(new_funcs)} NEW; {len(unresolved)} unresolved jr-dispatch sites)")
    if new_funcs:
        print("  new entries (vram size):")
        for f, s in sized[:80]:
            print(f"    0x{f:08x} 0x{s:x}")
        if len(sized) > 80:
            print(f"    ... (+{len(sized) - 80} more)")

    if args.report_only:
        return

    # Append new entries to function_adds.txt (skip ones already present).
    existing = set()
    try:
        for line in open(args.out, "r", encoding="utf-8"):
            s = line.split("#", 1)[0].strip()
            if s:
                existing.add(parse_int(s.split()[0]))
    except FileNotFoundError:
        pass
    to_write = [(f, s) for f, s in sized if f not in existing]
    if to_write:
        with open(args.out, "a", encoding="utf-8") as out:
            out.write("\n# --- auto-discovered by tools/mips_find_functions.py "
                      "(recursive-descent CFG walk) ---\n")
            for f, s in to_write:
                out.write(f"0x{f:08x} 0x{s:x}\n")
        print(f"  appended {len(to_write)} entr(ies) to {args.out}")
    else:
        print("  no new entries to append (all already present)")

    if unresolved:
        try:
            with open(args.unresolved_log, "w", encoding="utf-8") as lf:
                lf.write("# Unresolved indirect dispatch sites (jr $reg, non-$ra).\n")
                lf.write("# Each is a jump table / computed jump the static walk\n")
                lf.write("# could not enumerate. PC of the jr instruction:\n")
                for pc in sorted(set(unresolved)):
                    lf.write(f"0x{pc:08x}\n")
            print(f"  wrote {len(set(unresolved))} unresolved jr sites to {args.unresolved_log}")
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
