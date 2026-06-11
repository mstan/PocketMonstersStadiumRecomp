#!/usr/bin/env python3
"""
derive_boundary_merges.py — repair Ghidra over-split function boundaries
from the recompiler's "branching outside of the function" warnings.

When Ghidra splits one real function P into several pieces, P (clamped to
the next declared entry) ends early, and a branch from P to 0xTARGET (still
inside the REAL function) lands past P's clamped end -> "branching outside".

To repair: remove every declared function entry E with
    P_start < E.vram <= TARGET
so the next remaining entry after P is beyond TARGET; P's clamp then extends
to absorb the fragments and the branch becomes internal.

We do NOT remove an entry that is the destination of a `jal` anywhere in the
ROM (a genuine, separately-called function); if such an entry sits inside
the range we leave it and report it (that pair needs different handling).

Reads _recomp.log + functions.json + resident.bin; writes function_merges.txt.
"""
import re, json, struct, sys

LOG = sys.argv[1] if len(sys.argv) > 1 else "_recomp.log"
RES = sys.argv[2] if len(sys.argv) > 2 else "resident.bin"
FNS = sys.argv[3] if len(sys.argv) > 3 else "functions.json"
OUT = sys.argv[4] if len(sys.argv) > 4 else "function_merges.txt"

# Pairs of (branching function start, branch target).
pair_re = re.compile(
    r"(?:Function (FUN_[0-9A-Fa-f]+) is branching outside of the function "
    r"\(to (0x[0-9A-Fa-f]+)\)"
    r"|Unhandled branch in (FUN_[0-9A-Fa-f]+) at 0x[0-9A-Fa-f]+ to (0x[0-9A-Fa-f]+))")

pairs = []
for line in open(LOG, encoding="utf-8", errors="replace"):
    m = pair_re.search(line)
    if not m:
        continue
    if m.group(1):
        name, tgt = m.group(1), m.group(2)
    else:
        name, tgt = m.group(3), m.group(4)
    p_start = int(name[len("FUN_"):], 16)
    pairs.append((p_start, int(tgt, 16)))

fdata = json.load(open(FNS))
entry_vrams = sorted({f["vram"] for f in fdata["functions"]})

data = open(RES, "rb").read()
def is_jal_target(vram):
    word = 0x0C000000 | ((vram & 0x0FFFFFFF) >> 2)
    return data.find(struct.pack(">I", word)) != -1

SIZES = OUT.replace("merges", "sizes")
if SIZES == OUT:
    SIZES = "function_sizes.txt"

# Accumulate across passes: each recompile reveals a different subset of the
# over-splits (fixing one function shifts which branches surface), so we UNION
# with what previous passes already found. This makes the repair monotonic and
# idempotent — once clean, re-running adds nothing.
import bisect
merges, kept = set(), set()
need_size = {}      # P_start -> required byte size (cover furthest branch target)

def load_existing():
    try:
        for line in open(OUT, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                merges.add(int(line, 16))
    except IOError:
        pass
    try:
        for line in open(SIZES, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                v, s = line.split()
                need_size[int(v, 16)] = max(need_size.get(int(v, 16), 0), int(s, 16))
    except IOError:
        pass
load_existing()
for p_start, tgt in pairs:
    # Over-split fragments strictly between P and the target get absorbed.
    lo = bisect.bisect_right(entry_vrams, p_start)   # first entry > p_start
    hi = bisect.bisect_right(entry_vrams, tgt)       # entries <= tgt
    for v in entry_vrams[lo:hi]:
        if is_jal_target(v):
            kept.add(v)
        else:
            merges.add(v)
    # P must be large enough to contain the branch target instruction.
    req = (tgt - p_start) + 4
    if req > need_size.get(p_start, 0):
        need_size[p_start] = req

with open(OUT, "w", encoding="utf-8") as f:
    f.write("# function_merges.txt — over-split fragments to remove so the\n")
    f.write("# predecessor absorbs them. Auto-derived by\n")
    f.write("# tools/derive_boundary_merges.py from _recomp.log.\n")
    for v in sorted(merges):
        f.write("0x%08x\n" % v)

SIZES = OUT.replace("merges", "sizes")
if SIZES == OUT:
    SIZES = "function_sizes.txt"
with open(SIZES, "w", encoding="utf-8") as f:
    f.write("# function_sizes.txt — minimum byte size for functions Ghidra\n")
    f.write("# under-sized (branch target fell outside). 'vram size' per line.\n")
    f.write("# Auto-derived by tools/derive_boundary_merges.py.\n")
    for v in sorted(need_size):
        f.write("0x%08x 0x%x\n" % (v, need_size[v]))

print("branch-outside pairs this log: %d  | cumulative merges: %d  "
      "kept(jal'd): %d  size-overrides: %d"
      % (len(pairs), len(merges), len(kept), len(need_size)))
if kept:
    print("  kept (jal targets inside a branch range): %s"
          % [hex(x) for x in sorted(kept)][:20])
print("wrote %s and %s" % (OUT, SIZES))
