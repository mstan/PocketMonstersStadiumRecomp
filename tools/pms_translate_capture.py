#!/usr/bin/env python3
"""
pms_translate_capture.py — launch PMS-J armed (PMS_TEXTPROBE=1), sweep through
as many screens as possible to populate the always-on string inventory, then
dump it (stringdump.log). Feed that to tools/pms_build_translations.py.

Usage:
  python tools/pms_translate_capture.py [--port 49911] [--seconds 120]
"""
from __future__ import annotations
import argparse, json, os, socket, subprocess, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / "PocketMonstersStadiumRecomp.exe"
ROM = ROOT / "baserom.z64"
SHOT = ROOT / "tools" / "pms_screenshot.ps1"


class C:
    def __init__(self, port): self.port = port; self.f = None; self.s = None
    def connect(self, timeout=40.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.s = socket.create_connection(("127.0.0.1", self.port), timeout=1.0)
                self.f = self.s.makefile("rwb", buffering=0); self.cmd("ping"); return
            except OSError: time.sleep(0.25)
        raise RuntimeError("no debug server")
    def cmd(self, c, **kw):
        p = {"cmd": c}; p.update(kw)
        self.f.write((json.dumps(p, separators=(",", ":")) + "\n").encode())
        r = self.f.readline()
        if not r: raise RuntimeError("closed during " + c)
        d = json.loads(r.decode())
        if not d.get("ok"): raise RuntimeError(f"{c}: {d}")
        return d
    def close(self):
        try:
            if self.f: self.f.close()
            if self.s: self.s.close()
        except OSError: pass


def pulse(c, name, hold=0.12, gap=0.18):
    c.cmd("set_button", name=name, down=True); time.sleep(hold)
    c.cmd("set_button", name=name, down=False); time.sleep(gap)


def shoot(pid, label):
    if not SHOT.exists(): return
    out = ROOT / "build" / "pms_route_screens"; out.mkdir(parents=True, exist_ok=True)
    subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", str(SHOT),
                    str(out / f"{label}.png"), "-ProcessId", str(pid)],
                   cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=20, check=False)


# Directed traversal pattern that opens menus and submenus, then backs out.
# Repeated with variation to surface as much text as possible.
SEQS = [
    ["start", "start"],
    ["a"], ["b"],
    ["down", "a", "b"], ["up", "a", "b"],
    ["left", "a", "b"], ["right", "a", "b"],
    ["down", "down", "a", "b", "b"],
    ["right", "right", "a", "b", "b"],
    ["cu"], ["cd"], ["cl"], ["cr"], ["l"], ["r"], ["z"],
    ["start", "down", "a", "b", "start"],
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=49911)
    ap.add_argument("--seconds", type=float, default=120.0)
    a = ap.parse_args()

    for f in ("stringdump.log",):
        p = ROOT / "build" / f
        if p.exists(): p.unlink()

    env = os.environ.copy()
    env["PMS_TEXTPROBE"] = "1"
    env["PMS_DEBUG_PORT"] = str(a.port)
    env["PMS_VOLUME"] = "0.0"
    err = (ROOT / "_sweep.err.log").open("wb"); out = (ROOT / "_sweep.out.log").open("wb")
    proc = subprocess.Popen([str(EXE), str(ROM)], cwd=str(ROOT), env=env,
                            stdout=out, stderr=err,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    c = C(a.port)
    try:
        c.connect()
        print(f"[sweep] connected pid={proc.pid}; sweeping ~{a.seconds:.0f}s")
        time.sleep(5.0)
        shoot(proc.pid, "sweep_boot")
        end = time.time() + a.seconds
        i = 0
        while time.time() < end:
            seq = SEQS[i % len(SEQS)]
            for b in seq:
                pulse(c, b)
            if i % 8 == 0:
                shoot(proc.pid, f"sweep_{i:03d}")
            i += 1
        shoot(proc.pid, "sweep_final")
        print(f"[sweep] {i} sequences sent; dumping inventory")
        print("[sweep] stringdump ->", c.cmd("stringdump"))
        time.sleep(0.3)
    finally:
        try: c.cmd("quit")
        except Exception: pass
        c.close()
        try: proc.wait(timeout=5)
        except Exception: proc.kill()
        err.close(); out.close()

    log = ROOT / "build" / "stringdump.log"
    if log.exists():
        n = sum(1 for ln in log.read_text(encoding="utf-8", errors="replace").splitlines()
                if ln.startswith("0x"))
        print(f"[sweep] stringdump.log: {n} distinct strings")
    else:
        print("[sweep] WARNING: no stringdump.log")


if __name__ == "__main__":
    raise SystemExit(main())
