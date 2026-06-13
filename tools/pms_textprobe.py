#!/usr/bin/env python3
"""
pms_textprobe.py — drive PMS-J to a text-bearing screen, then dump the
always-on text-draw census (diagnostics.cpp) to identify the string-draw
function empirically.

The census is armed by env PMS_TEXTPROBE=1 and records, from process start,
every recompiled-function entry whose args match the string-draw signature
(a0,a1 small screen coords; a2 -> NUL-terminated nonzero RDRAM bytes). The
real string-drawer is the FUN_ called far more than any incidental match,
with a2 pointing at glyph byte-strings — that sample is also the live
encoding inventory.

Usage:
  python tools/pms_textprobe.py [--port 49907] [--seconds 8]
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / "PocketMonstersStadiumRecomp.exe"
ROM = ROOT / "baserom.z64"
SCREENSHOT = ROOT / "tools" / "pms_screenshot.ps1"
LOG = ROOT / "textdraw_probe.log"


class DebugClient:
    def __init__(self, port: int):
        self.port = port
        self.sock = None
        self.file = None

    def connect(self, timeout: float = 40.0) -> None:
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=1.0)
                self.file = self.sock.makefile("rwb", buffering=0)
                self.command("ping")
                return
            except OSError as exc:
                last = exc
                time.sleep(0.25)
        raise RuntimeError(f"debug server did not accept connections: {last}")

    def command(self, cmd: str, **kwargs) -> dict:
        payload = {"cmd": cmd}
        payload.update(kwargs)
        line = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.file.write(line)
        raw = self.file.readline()
        if not raw:
            raise RuntimeError(f"server closed during {cmd}")
        resp = json.loads(raw.decode("utf-8"))
        if not resp.get("ok", False):
            raise RuntimeError(f"{cmd} failed: {resp}")
        return resp

    def close(self) -> None:
        try:
            if self.file:
                self.file.close()
            if self.sock:
                self.sock.close()
        except OSError:
            pass


def pulse(c: DebugClient, name: str, hold: float = 0.1, gap: float = 0.15) -> None:
    c.command("set_button", name=name, down=True)
    time.sleep(hold)
    c.command("set_button", name=name, down=False)
    time.sleep(gap)


def shoot(pid: int, label: str) -> None:
    if not SCREENSHOT.exists():
        return
    out = ROOT / "build" / "pms_route_screens"
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(SCREENSHOT),
         str(out / f"{label}.png"), "-ProcessId", str(pid)],
        cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        timeout=20, check=False)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=49907)
    ap.add_argument("--seconds", type=float, default=8.0, help="settle time per screen")
    args = ap.parse_args()

    if LOG.exists():
        LOG.unlink()

    env = os.environ.copy()
    env["PMS_TEXTPROBE"] = "1"
    env["PMS_DEBUG_PORT"] = str(args.port)
    env["PMS_BOOT_WATCHDOG"] = "1"
    env["PMS_VOLUME"] = "0.0"

    err = (ROOT / "_textprobe.err.log").open("wb")
    out = (ROOT / "_textprobe.out.log").open("wb")
    proc = subprocess.Popen(
        [str(EXE), str(ROM)], cwd=str(ROOT), env=env, stdout=out, stderr=err,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    c = DebugClient(args.port)
    try:
        c.connect()
        print(f"[textprobe] connected (pid={proc.pid}); booting to menu...")
        time.sleep(args.seconds)
        shoot(proc.pid, "textprobe_boot")
        # Press Start / move around to surface press-start -> main menu text.
        for _ in range(3):
            pulse(c, "start", hold=0.15, gap=0.5)
        time.sleep(2.0)
        shoot(proc.pid, "textprobe_menu1")
        for nm in ("down", "down", "up", "a", "b"):
            pulse(c, nm)
        time.sleep(args.seconds)
        shoot(proc.pid, "textprobe_menu2")
        resp = c.command("textdump")
        print(f"[textprobe] textdump -> {resp}")
        resp = c.command("fontdump")
        print(f"[textprobe] fontdump -> {resp}")
        time.sleep(0.3)
    finally:
        try:
            c.command("quit")
        except Exception:
            pass
        c.close()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        err.close()
        out.close()

    if LOG.exists():
        print("\n========== textdraw_probe.log ==========")
        sys.stdout.write(LOG.read_text(encoding="utf-8", errors="replace"))
    else:
        print("[textprobe] WARNING: no textdraw_probe.log produced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
