#!/usr/bin/env python3
"""pms_cmd.py — send raw debug-server commands and print the JSON replies.

Leaves the runner alive (never sends quit). Useful for textdump/stringdump/
fontdump/dump_sections/etc. without baking each into pms_drive.py.

Usage:
  python tools/pms_cmd.py --port 49911 textdump stringdump fontdump
  python tools/pms_cmd.py --port 49911 status
"""
from __future__ import annotations
import argparse, json, socket, sys


def main(argv) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=49911)
    ap.add_argument("cmds", nargs="+")
    a = ap.parse_args(argv)
    s = socket.create_connection(("127.0.0.1", a.port), timeout=8)
    f = s.makefile("rwb", buffering=0)
    for c in a.cmds:
        f.write((json.dumps({"cmd": c}) + "\n").encode())
        line = f.readline()
        print(f"{c} -> {line.decode().strip() if line else '(no reply)'}")
    f.close(); s.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
