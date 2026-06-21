#!/usr/bin/env python3
"""Thin interactive driver for the PMS debug TCP server.

Unlike pms_debug_fuzz.py, this connects, runs a sequence of actions, and
LEAVES THE RUNNER ALIVE (never sends `quit`) so you can iterate:
screenshot, look, decide the next move, drive again.

Actions (positional, executed in order):
  boot:<vi>        wait until status.vi >= <vi> (default timeout 90s)
  wait:<seconds>   sleep
  press:<button>   pulse a button (hold 8 frames, gap 4)
  hold:<btn>:<f>   hold a button for <f> frames then release
  stick:<x>,<y>    hold stick at x,y for 8 frames then center
  shot:<label>     screenshot to build/pms_route_screens/<label>.png
  status           print status JSON
  captures         print build/runtime_captures.json if present

Example:
  python tools/pms_drive.py --port 49930 boot:180 shot:menu status
"""
from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCREENSHOT = ROOT / "tools" / "pms_screenshot.ps1"
CAPTURES = ROOT / "build" / "runtime_captures.json"


class DebugClient:
    def __init__(self, port: int):
        self.port = port
        self.sock = None
        self.file = None

    def connect(self, timeout: float = 30.0) -> None:
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

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
        if self.sock is not None:
            self.sock.close()

    def command(self, cmd: str, **kw) -> dict:
        payload = {"cmd": cmd}
        payload.update(kw)
        self.file.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
        raw = self.file.readline()
        if not raw:
            raise RuntimeError(f"debug server closed while handling {cmd}")
        resp = json.loads(raw.decode())
        if not resp.get("ok", False):
            raise RuntimeError(f"{cmd} failed: {resp}")
        return resp


def sleep_frames(frames: int, fps: float = 60.0) -> None:
    time.sleep(max(0.0, frames / fps))


def shot(label: str, pid: int) -> None:
    out_dir = ROOT / "build" / "pms_route_screens"
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{label}.png"
    subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(SCREENSHOT),
         str(path), "-ProcessId", str(pid)],
        cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        timeout=20, check=False)
    print(f"shot -> {path}")


def main(argv) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=49930)
    ap.add_argument("--pid", type=int, default=0, help="runner pid for screenshots")
    ap.add_argument("actions", nargs="*")
    args = ap.parse_args(argv)

    c = DebugClient(args.port)
    c.connect()
    try:
        for act in args.actions:
            if act.startswith("boot:"):
                target = int(act.split(":", 1)[1])
                deadline = time.time() + 120.0
                st = {}
                while time.time() < deadline:
                    st = c.command("status")
                    if int(st.get("vi", 0)) >= target:
                        break
                    time.sleep(0.25)
                print(f"boot vi={st.get('vi')}")
            elif act.startswith("wait:"):
                time.sleep(float(act.split(":", 1)[1]))
            elif act.startswith("press:"):
                name = act.split(":", 1)[1]
                c.command("set_button", name=name, down=True)
                sleep_frames(8)
                c.command("set_button", name=name, down=False)
                sleep_frames(4)
            elif act.startswith("hold:"):
                _, name, frames = act.split(":")
                c.command("set_button", name=name, down=True)
                sleep_frames(int(frames))
                c.command("set_button", name=name, down=False)
                sleep_frames(4)
            elif act.startswith("stick:"):
                x, y = act.split(":", 1)[1].split(",")
                c.command("set_stick", x=int(x), y=int(y))
                sleep_frames(8)
                c.command("set_stick", x=0, y=0)
                sleep_frames(4)
            elif act.startswith("shot:"):
                shot(act.split(":", 1)[1], args.pid)
            elif act == "status":
                print("status " + json.dumps(c.command("status")))
            elif act == "captures":
                if CAPTURES.exists():
                    print(CAPTURES.read_text())
                else:
                    print("(no runtime_captures.json yet)")
            elif act == "dumpsec":
                print("dump_sections " + json.dumps(c.command("dump_sections")))
            elif act == "ptrsite":
                print("probe_pointer_site " + json.dumps(c.command("probe_pointer_site")))
            elif act.startswith("probe:"):
                addr = act.split(":", 1)[1]
                print("probe_lookup " + json.dumps(c.command("probe_lookup", addr=addr)))
            elif act.startswith("jit:"):
                addr = act.split(":", 1)[1]
                print("jit_test " + json.dumps(c.command("jit_test", addr=addr)))
            elif act == "evictall":
                print("jit_evict_all " + json.dumps(c.command("jit_evict_all")))
            elif act.startswith("mem:"):
                parts = act.split(":")
                addr = parts[1]
                length = int(parts[2]) if len(parts) > 2 else 256
                print("read_mem " + json.dumps(c.command("read_mem", addr=addr, len=length)))
            elif act.startswith("tracedump"):
                tag = act.split(":", 1)[1] if ":" in act else "manual"
                print("tracedump " + json.dumps(c.command("tracedump", tag=tag)))
            elif act.startswith("dump_sched"):
                tl = act.split(":", 1)[1] if ":" in act else "600"
                print("dump_sched " + json.dumps(c.command("dump_sched", tail=tl)))
            elif act.startswith("dump_mesg"):
                rest = act.split(":")[1:]
                mq = rest[0] if len(rest) > 0 and rest[0] else "0"
                print("dump_mesg " + json.dumps(c.command("dump_mesg", mq=mq)))
            else:
                print(f"unknown action: {act}", file=sys.stderr)
        c.command("clear_input")
    finally:
        c.close()  # NOTE: never `quit` — leave runner alive
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
