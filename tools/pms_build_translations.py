#!/usr/bin/env python3
"""
pms_build_translations.py — decode the captured string inventory (stringdump.log)
from EUC-JP to Japanese, and (optionally) join a JP->EN dictionary into a
ready-to-load translations.json.

Two modes:
  decode  : stringdump.log -> strings_decoded.tsv (key, count, x, y, src_hex, jp)
            Always run this first; read the TSV to see the Japanese.
  build   : strings_decoded.tsv + jp_en.tsv (japanese<TAB>english) ->
            build/translations.json  (matches by exact decoded Japanese)

The game text is EUC-JP-ish (JIS X 0208 via A1/A3/A4/A5 lead bytes). Python's
'euc_jp' codec decodes it. Bytes that don't decode are kept as \\xHH so the
TSV still round-trips and odd entries are visible.

Usage:
  python tools/pms_build_translations.py decode
  python tools/pms_build_translations.py build
"""
from __future__ import annotations
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DUMP = ROOT / "build" / "stringdump.log"
TSV = ROOT / "tools" / "strings_decoded.tsv"
JPEN = ROOT / "tools" / "jp_en.tsv"
OUT = ROOT / "build" / "translations.json"


def decode_euc(b: bytes) -> str:
    # tolerant: decode as much as possible, escaping undecodable bytes
    try:
        return b.decode("euc_jp")
    except UnicodeDecodeError:
        out = []
        i = 0
        while i < len(b):
            for n in (2, 1):
                try:
                    out.append(b[i:i + n].decode("euc_jp")); i += n; break
                except Exception:
                    if n == 1:
                        out.append(f"\\x{b[i]:02x}"); i += 1
        return "".join(out)


def parse_dump():
    rows = []
    if not DUMP.exists():
        print(f"missing {DUMP} — run tools/pms_translate_capture.py first")
        return rows
    for ln in DUMP.read_text(encoding="utf-8", errors="replace").splitlines():
        if not ln.startswith("0x"):
            continue
        parts = ln.split()
        if len(parts) < 6:
            continue
        key, count, x, y, length, src_hex = parts[0], parts[1], parts[2], parts[3], parts[4], parts[5]
        try:
            raw = bytes.fromhex(src_hex)
        except ValueError:
            continue
        rows.append({"key": key, "count": int(count), "x": int(x), "y": int(y),
                     "src_hex": src_hex, "jp": decode_euc(raw)})
    return rows


def load_tsv():
    master = {}  # key -> row
    if TSV.exists():
        lines = TSV.read_text(encoding="utf-8", errors="replace").splitlines()
        for ln in lines[1:]:
            p = ln.split("\t")
            if len(p) < 6:
                continue
            master[p[0]] = {"key": p[0], "count": int(p[1]) if p[1].isdigit() else 0,
                            "x": int(p[2]) if p[2].lstrip('-').isdigit() else 0,
                            "y": int(p[3]) if p[3].lstrip('-').isdigit() else 0,
                            "src_hex": p[4], "jp": p[5]}
    return master


def write_tsv(master):
    rows = sorted(master.values(), key=lambda r: -r["count"])
    with TSV.open("w", encoding="utf-8") as f:
        f.write("key\tcount\tx\ty\tsrc_hex\tjp\n")
        for r in rows:
            jp = r["jp"].replace("\t", " ").replace("\n", "\\n")
            f.write(f'{r["key"]}\t{r["count"]}\t{r["x"]}\t{r["y"]}\t{r["src_hex"]}\t{jp}\n')
    return rows


def cmd_decode():
    # Accumulate into a persistent master so captures survive across sessions
    # (the in-game stringdump.log is overwritten on each launch).
    master = load_tsv()
    added = 0
    for r in parse_dump():
        r = dict(r); r["jp"] = r["jp"]
        if r["key"] not in master:
            added += 1
        # keep the higher count / latest coords
        if r["key"] in master:
            r["count"] = max(r["count"], master[r["key"]]["count"])
        master[r["key"]] = r
    rows = write_tsv(master)
    print(f"wrote {TSV}: {len(rows)} strings total (+{added} new this capture)")


def json_escape(s: str) -> str:
    out = []
    for ch in s:
        if ch == '"': out.append('\\"')
        elif ch == '\\': out.append('\\\\')
        elif ch == '\n': out.append('\\n')
        elif ch == '\t': out.append('\\t')
        else: out.append(ch)
    return "".join(out)


KEYEN = ROOT / "tools" / "key_en.tsv"


def cmd_build():
    # Join key_en.tsv (<key><TAB><english>) against the persistent master TSV
    # (key -> src_hex). Keying by the ASCII FNV key avoids transcribing Japanese.
    # The master accumulates across sessions; the in-game stringdump.log alone is
    # transient (overwritten per launch).
    rows = {k.lower(): v for k, v in load_tsv().items()}
    for r in parse_dump():  # also fold in the current session's dump
        rows.setdefault(r["key"].lower(), r)
    if not KEYEN.exists():
        print(f"missing {KEYEN} — create it: <0xKEY><TAB><english> per line")
        return
    entries = []
    miss = 0
    for ln in KEYEN.read_text(encoding="utf-8").splitlines():
        s = ln.rstrip("\n")
        if not s.strip() or s.lstrip().startswith("#") or "\t" not in s:
            continue
        key, en = s.split("\t", 1)
        key = key.strip().lower()
        r = rows.get(key)
        if r is None:
            miss += 1
            print(f"  [no match] {key}")
            continue
        # english uses literal \n in the TSV -> real newline in JSON output
        en = en.replace("\\n", "\n")
        entries.append((r["src_hex"], r["jp"], en))
    # MERGE-PRESERVE: never silently drop entries already in translations.json.
    # Some strings were authored straight into the JSON (e.g. the battle-screen
    # set in commit 7bc8a68) and have no key_en.tsv row; a from-scratch rebuild
    # would lose them. Carry forward any existing entry whose src_hex isn't
    # (re)produced from key_en. key_en always wins on conflict (it's first).
    have = {sh.lower() for sh, _, _ in entries}
    preserved = 0
    if OUT.exists():
        try:
            for e in json.loads(OUT.read_text(encoding="utf-8")):
                sh = str(e.get("src_hex", ""))
                if sh and sh.lower() not in have:
                    entries.append((sh, e.get("src_jp", ""), e.get("target", "")))
                    have.add(sh.lower())
                    preserved += 1
        except (ValueError, OSError) as exc:
            print(f"  [warn] could not merge existing {OUT.name}: {exc}")
    with OUT.open("w", encoding="utf-8") as f:
        f.write("[\n")
        for i, (sh, jp, en) in enumerate(entries):
            comma = "," if i + 1 < len(entries) else ""
            f.write(f'  {{ "src_hex": "{sh}", "src_jp": "{json_escape(jp)}", '
                    f'"target": "{json_escape(en)}" }}{comma}\n')
        f.write("]\n")
    print(f"wrote {OUT}: {len(entries)} entries "
          f"({miss} unmatched keys, {preserved} preserved from existing JSON)")


def cmd_todo():
    # List master strings that have no key_en.tsv entry yet -> tools/untranslated.tsv
    have = set()
    if KEYEN.exists():
        for ln in KEYEN.read_text(encoding="utf-8").splitlines():
            s = ln.strip()
            if s and not s.startswith("#") and "\t" in s:
                have.add(s.split("\t", 1)[0].strip().lower())
    master = load_tsv()
    todo = [r for k, r in master.items() if k.lower() not in have]
    todo.sort(key=lambda r: -r["count"])
    out = ROOT / "tools" / "untranslated.tsv"
    with out.open("w", encoding="utf-8") as f:
        f.write("key\tcount\tx\ty\tjp\n")
        for r in todo:
            f.write(f'{r["key"]}\t{r["count"]}\t{r["x"]}\t{r["y"]}\t{r["jp"]}\n')
    print(f"wrote {out}: {len(todo)} untranslated (of {len(master)} total)")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "decode"
    if mode == "decode": cmd_decode()
    elif mode == "build": cmd_build()
    elif mode == "todo": cmd_todo()
    else: print("usage: pms_build_translations.py [decode|build|todo]")


if __name__ == "__main__":
    raise SystemExit(main())
