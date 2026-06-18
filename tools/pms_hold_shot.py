#!/usr/bin/env python3
"""Hold a button down, screenshot, then release — for views that only show
while a button is held (e.g. PMS battle move-detail = hold R).

Usage: python tools/pms_hold_shot.py --port 49911 --pid 1234 --btn r --label foo
"""
from __future__ import annotations
import argparse, json, socket, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHOT = ROOT / "tools" / "pms_screenshot.ps1"


def main(argv) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=49911)
    ap.add_argument("--pid", default="0")
    ap.add_argument("--btn", default="r")
    ap.add_argument("--label", default="hold_shot")
    a = ap.parse_args(argv)
    s = socket.create_connection(("127.0.0.1", a.port), timeout=8)
    f = s.makefile("rwb", buffering=0)

    def cmd(d):
        f.write((json.dumps(d) + "\n").encode())
        return f.readline()

    cmd({"cmd": "set_button", "name": a.btn, "down": True})
    time.sleep(0.5)
    out = ROOT / "build" / "pms_route_screens" / f"{a.label}.png"
    subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", str(SHOT),
                    str(out), "-ProcessId", str(a.pid)], cwd=str(ROOT), check=False)
    time.sleep(0.2)
    cmd({"cmd": "set_button", "name": a.btn, "down": False})
    cmd({"cmd": "clear_input"})
    f.close(); s.close()
    print(f"held {a.btn}, shot -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
