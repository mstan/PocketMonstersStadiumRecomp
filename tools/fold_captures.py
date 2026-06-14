#!/usr/bin/env python3
"""fold_captures.py — promote runtime-discovered function entries to static.

The "self-improving" tier of the overlay-discovery arc. At runtime, the
librecomp lookup-miss self-heal resolves an interior get_function miss by
dispatching into the resident enclosing function — but it re-scans on every
hit because the interior point is never registered in func_map. This tool
folds those discovered entries back into game.toml's force_function_vrams,
so the next regen turns each into a REGISTERED dispatch alias: get_function
then resolves it directly on the static fast path (no miss, no scan).

Loop:  drive -> build/runtime_captures.json (always-on manifest)
            -> fold_captures.py (this)  -> game.toml force_function_vrams
            -> regen -> registered dispatch aliases -> lookup_misses fall to 0

It manages ONLY the delimited region between the BEGIN/END markers inside the
force_function_vrams array; manual entries elsewhere in the array are preserved
verbatim. Cumulative: existing folded addresses are unioned with new captures,
so coverage accumulates across separate drives. Idempotent.

What it folds:
  - Genuine interior code entries: a miss whose offset carries NO relocation
    (reloc_at_offset == false). The missed address IS the code entry; seed it.
What it SKIPS (loudly, never silently):
  - pointer-site misses (reloc_at_offset == true): the missed value is a
    relocated POINTER, not a code entry; the true target is
    section[reloc_target_section]:reloc_target_offset, which this manifest
    cannot resolve to an absolute vram. Reported for manual handling.
  - offset_is_known_func: a miss on a registered function START points at an
    eviction/aliasing bug, not a discovery gap — folding would mask it.
  - no-sections / classification we don't understand.

Regen is the validator: a folded vram that doesn't land cleanly in a
decompressed section is reported by N64Recomp as
"WARNING forced vram ... could not be emitted" — this tool folds
optimistically and lets the build confirm.

Usage:
  python tools/fold_captures.py                 # fold build/runtime_captures.json into game.toml
  python tools/fold_captures.py --captures P --toml Q
  python tools/fold_captures.py --dry-run       # print what would change, write nothing
"""
import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BEGIN = "# >>> BEGIN auto-folded from runtime_captures.json -- managed by tools/fold_captures.py"
END = "# <<< END auto-folded (do not edit between markers by hand)"

ADDR_RE = re.compile(r"0x[0-9A-Fa-f]+")


def load_foldable(captures_path: Path):
    """Return (fold: dict addr->comment, skipped: list of (addr, reason))."""
    data = json.loads(captures_path.read_text(encoding="utf-8"))
    fold = {}
    skipped = []
    for m in data.get("misses", []):
        addr = int(str(m["missed_addr"]), 16)
        cls = m.get("classification", "?")
        hits = m.get("hit_count", 0)
        if m.get("reloc_at_offset"):
            skipped.append((addr,
                f"pointer-site ({m.get('reloc_type','?')} -> "
                f"section[{m.get('reloc_target_section')}]:"
                f"{m.get('reloc_target_offset')}); resolve target manually"))
            continue
        if m.get("offset_is_known_func"):
            skipped.append((addr, "miss on a known function START "
                                  "(eviction/aliasing bug, not a discovery gap)"))
            continue
        if cls == "no-sections":
            skipped.append((addr, "no-sections (sections not initialized)"))
            continue
        fold[addr] = f"{cls} hit_count={hits}"
    return fold, skipped


def find_array_block(text: str):
    """Locate `force_function_vrams = [ ... ]`. Return (start, end, inner)."""
    m = re.search(r"force_function_vrams\s*=\s*\[", text)
    if not m:
        raise SystemExit("error: force_function_vrams array not found in game.toml")
    open_br = text.index("[", m.start())
    # find matching closing bracket (no nested brackets expected in this array)
    close_br = text.index("]", open_br)
    return open_br, close_br, text[open_br + 1:close_br]


def parse_existing(inner: str):
    """Split inner into (preamble_lines, folded: dict addr->comment).

    preamble = everything NOT inside the BEGIN/END marker region (manual
    entries + comments), preserved verbatim. folded = addresses currently in
    the managed region (so we accumulate across runs)."""
    lines = inner.splitlines()
    preamble = []
    folded = {}
    in_region = False
    for ln in lines:
        if BEGIN in ln:
            in_region = True
            continue
        if END in ln:
            in_region = False
            continue
        if in_region:
            mm = ADDR_RE.search(ln)
            if mm:
                addr = int(mm.group(0), 16)
                comment = ""
                hashpos = ln.find("#")
                if hashpos != -1:
                    comment = ln[hashpos + 1:].strip()
                folded[addr] = comment
        else:
            preamble.append(ln)
    # Trim trailing blank lines from preamble for tidy output
    while preamble and preamble[-1].strip() == "":
        preamble.pop()
    return preamble, folded


def render_block(preamble, folded):
    out = ["force_function_vrams = ["]
    for ln in preamble:
        out.append(ln)
    out.append("    " + BEGIN)
    for addr in sorted(folded):
        c = folded[addr]
        suffix = f"  # {c}" if c else ""
        out.append(f"    0x{addr:08X},{suffix}")
    out.append("    " + END)
    out.append("]")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--captures", default=str(REPO / "build" / "runtime_captures.json"))
    ap.add_argument("--toml", default=str(REPO / "game.toml"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    captures_path = Path(args.captures)
    toml_path = Path(args.toml)
    if not captures_path.exists():
        raise SystemExit(f"error: captures file not found: {captures_path}")

    new_fold, skipped = load_foldable(captures_path)

    text = toml_path.read_text(encoding="utf-8")
    open_br, close_br, inner = find_array_block(text)
    preamble, existing = parse_existing(inner)

    merged = dict(existing)
    added = []
    for addr, comment in new_fold.items():
        if addr not in merged:
            added.append(addr)
        merged[addr] = comment  # refresh comment with latest classification/hits

    # render_block emits the whole `force_function_vrams = [ ... ]`; splice it
    # in place of the original assignment (from the keyword to the closing ]).
    block = render_block(preamble, merged)
    assign_start = re.search(r"force_function_vrams\s*=\s*\[", text).start()
    new_text = text[:assign_start] + block + text[close_br + 1:]

    # ---- report ----
    print(f"captures : {captures_path}")
    print(f"game.toml: {toml_path}")
    print(f"existing folded entries: {len(existing)}")
    print(f"foldable code entries in capture: {len(new_fold)}")
    if added:
        print(f"NEW entries folded ({len(added)}):")
        for a in sorted(added):
            print(f"  + 0x{a:08X}  ({merged[a]})")
    else:
        print("NEW entries folded: 0 (already up to date)")
    if skipped:
        print(f"SKIPPED ({len(skipped)}) — NOT folded, need attention:")
        for a, reason in sorted(skipped):
            print(f"  ! 0x{a:08X}  {reason}")
    print(f"total managed entries after fold: {len(merged)}")

    if args.dry_run:
        print("\n--dry-run: game.toml NOT modified.")
        return

    if new_text == text:
        print("\ngame.toml already up to date — no write.")
        return
    toml_path.write_text(new_text, encoding="utf-8")
    print("\ngame.toml updated. Next: regen + rebuild, then re-drive to confirm "
          "lookup_misses fall to 0 (folded entries become static_dispatch_hits).")


if __name__ == "__main__":
    main()
