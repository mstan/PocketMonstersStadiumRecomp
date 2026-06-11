#!/usr/bin/env python3
"""
Launch and drive PocketMonstersStadiumRecomp through the PMS debug TCP server.

Examples:
  python tools/pms_debug_fuzz.py --scenario boot --seconds 20
  python tools/pms_debug_fuzz.py --scenario attract --seconds 90
  python tools/pms_debug_fuzz.py --scenario random --seconds 120 --seed 1
"""

from __future__ import annotations

import argparse
import json
import os
import random
import socket
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / "PocketMonstersStadiumRecomp.exe"
ROM = ROOT / "baserom.z64"
SCREENSHOT = ROOT / "tools" / "pms_screenshot.ps1"

BAD_LOG_PATTERNS = [
    "bad function pointer",
    "get_function lookup miss",
    "[PMS ERROR]",
    "std::terminate",
    "SIGABRT",
    "Unhandled `",
    "[rsp watchdog]",
]

BUTTONS = [
    "a",
    "b",
    "z",
    "start",
    "up",
    "down",
    "left",
    "right",
    "l",
    "r",
    "c-up",
    "c-down",
    "c-left",
    "c-right",
]


class DebugClient:
    def __init__(self, port: int):
        self.port = port
        self.sock: socket.socket | None = None
        self.file = None

    def connect(self, timeout: float = 30.0) -> None:
        deadline = time.time() + timeout
        last_error: OSError | None = None
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=1.0)
                self.file = self.sock.makefile("rwb", buffering=0)
                self.command("ping")
                return
            except OSError as exc:
                last_error = exc
                time.sleep(0.25)
        raise RuntimeError(f"debug server did not accept connections: {last_error}")

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def command(self, cmd: str, **kwargs) -> dict:
        if self.file is None:
            raise RuntimeError("debug client is not connected")
        payload = {"cmd": cmd}
        payload.update(kwargs)
        line = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.file.write(line)
        raw = self.file.readline()
        if not raw:
            raise RuntimeError(f"debug server closed while handling {cmd}")
        response = json.loads(raw.decode("utf-8"))
        if not response.get("ok", False):
            raise RuntimeError(f"{cmd} failed: {response}")
        return response


def start_runner(port: int, err_log: Path, out_log: Path, turbo: bool, volume: float) -> subprocess.Popen:
    env = os.environ.copy()
    env["PMS_DEBUG_PORT"] = str(port)
    env["PMS_BOOT_WATCHDOG"] = "1"
    env["PMS_TURBO"] = "1" if turbo else "0"
    env["PMS_VOLUME"] = str(volume)
    env.pop("PSR_FUNC_MAP_PROBE", None)
    env.pop("PMS_SCRIPT_DIAG", None)
    err = err_log.open("wb")
    out = out_log.open("wb")
    try:
        return subprocess.Popen(
            [str(EXE), str(ROM)],
            cwd=str(ROOT),
            env=env,
            stdout=out,
            stderr=err,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    except Exception:
        err.close()
        out.close()
        raise


def wait_vi(client: DebugClient, target: int, timeout: float) -> dict:
    deadline = time.time() + timeout
    status = {}
    while time.time() < deadline:
        status = client.command("status")
        if int(status.get("vi", 0)) >= target:
            return status
        time.sleep(0.25)
    raise RuntimeError(f"timed out waiting for vi >= {target}; last status: {status}")


def sleep_frames(frames: int, fps: float = 60.0) -> None:
    time.sleep(max(0.0, frames / fps))


def pulse(client: DebugClient, name: str, hold_frames: int = 5, gap_frames: int = 5) -> None:
    client.command("set_button", name=name, down=True)
    sleep_frames(hold_frames)
    client.command("set_button", name=name, down=False)
    sleep_frames(gap_frames)


def capture_window(label: str, proc: subprocess.Popen) -> Path | None:
    if not SCREENSHOT.exists():
        return None
    out_dir = ROOT / "build" / "pms_route_screens"
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{label}.png"
    subprocess.run(
        [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(SCREENSHOT),
            str(path),
            "-ProcessId",
            str(proc.pid),
        ],
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=15,
        check=False,
    )
    return path


def hold_stick(client: DebugClient, x: int, y: int, hold_frames: int = 8) -> None:
    client.command("set_stick", x=x, y=y)
    sleep_frames(hold_frames)
    client.command("set_stick", x=0, y=0)
    sleep_frames(4)


def scenario_boot(client: DebugClient, seconds: float) -> None:
    wait_vi(client, max(60, int(seconds * 60)), timeout=seconds + 45.0)


def scenario_attract(client: DebugClient, seconds: float) -> None:
    wait_vi(client, max(120, int(seconds * 60)), timeout=seconds + 60.0)


def scenario_scripted_menu(client: DebugClient, seconds: float) -> None:
    wait_vi(client, 180, timeout=60.0)
    end = time.time() + seconds
    sequence = [
        "start", "a", "down", "a", "b",
        "right", "a", "b", "left", "a", "b",
        "up", "a", "b", "start", "b",
    ]
    while time.time() < end:
        for button in sequence:
            pulse(client, button)
            if time.time() >= end:
                break


def scenario_registration_quit(client: DebugClient, proc: subprocess.Popen) -> None:
    wait_vi(client, 180, timeout=60.0)
    route = [
        ("start", 4.5, "s00_after_start1"),
        ("start", 4.5, "s01_title"),
        ("a", 4.5, "s02_gamepak_check"),
        ("a", 4.5, "s03_select"),
        ("a", 4.5, "s04_stadium_hub"),
        ("a", 4.0, "s05_cup_select"),
        ("a", 4.0, "s06_division_select"),
        ("a", 4.0, "s07_stadium_screen"),
        ("right", 1.2, "s08_right1"),
        ("right", 1.2, "s09_registration"),
        ("a", 3.5, "s10_registration_submenu"),
        ("down", 0.9, "s11_down1"),
        ("down", 0.9, "s12_down2"),
        ("down", 0.9, "s13_quit_highlighted"),
        ("a", 2.0, "s14_after_quit"),
        ("b", 1.0, "s15_back1"),
        ("b", 1.0, "s16_back2"),
        ("b", 1.0, "s17_back3"),
    ]
    for button, wait_s, label in route:
        if proc.poll() is not None:
            raise RuntimeError(f"runner exited during registration-quit route at {label}: {proc.returncode}")
        pulse(client, button, hold_frames=8, gap_frames=4)
        time.sleep(wait_s)
        capture_window(label, proc)
        client.command("status")


BattleStep = tuple[str, float, str]


BATTLE_ENTRY_ROUTE: list[BattleStep] = [
    ("start", 4.5, "battle_s00_after_start1"),
    ("start", 4.5, "battle_s01_title"),
    ("a", 4.5, "battle_s02_gamepak_check"),
    ("a", 4.5, "battle_s03_select"),
    ("a", 4.5, "battle_s04_stadium_hub"),
    ("a", 4.0, "battle_s05_cup_select"),
    ("a", 4.0, "battle_s06_division_select"),
    ("a", 4.0, "battle_s07_com_select"),
    ("a", 3.5, "battle_s08_team_list"),
    ("down", 0.9, "battle_s09_team_down1"),
    ("down", 0.9, "battle_s10_team_down2"),
    ("down", 0.9, "battle_s11_team_down3"),
    ("a", 2.0, "battle_s12_entry_confirm"),
    ("a", 4.0, "battle_s13_after_confirm"),
    ("a", 4.0, "battle_s14_advance1"),
    ("a", 4.0, "battle_s15_advance2"),
    ("a", 4.0, "battle_s16_advance3"),
    ("a", 4.0, "battle_s17_advance4"),
]


def drive_route(client: DebugClient, proc: subprocess.Popen, route: list[BattleStep], name: str) -> None:
    for button, wait_s, label in route:
        if proc.poll() is not None:
            raise RuntimeError(f"runner exited during {name} route at {label}: {proc.returncode}")
        pulse(client, button, hold_frames=8, gap_frames=4)
        time.sleep(wait_s)
        capture_window(label, proc)
        client.command("status")


def scenario_battle_entry(client: DebugClient, proc: subprocess.Popen) -> None:
    wait_vi(client, 180, timeout=60.0)
    drive_route(client, proc, BATTLE_ENTRY_ROUTE, "battle-entry")


def scenario_battle_play(client: DebugClient, proc: subprocess.Popen) -> None:
    wait_vi(client, 180, timeout=60.0)
    route = BATTLE_ENTRY_ROUTE + [
        ("a", 4.0, "battle_s18_confirm_party"),
        ("r", 8.0, "battle_s19_r_confirm_order"),
        ("a", 8.0, "battle_s20_intro_advance2"),
        ("a", 8.0, "battle_s21_sendout"),
        ("a", 6.0, "battle_s22_prompt_or_menu"),
        ("a", 5.0, "battle_s23_default_choice"),
        ("a", 8.0, "battle_s24_after_default"),
        ("b", 3.0, "battle_s25_back_or_cancel"),
        ("start", 3.0, "battle_s26_start_menu"),
        ("b", 3.0, "battle_s27_back_after_start"),
        ("a", 5.0, "battle_s28_confirm_after_start"),
    ]
    drive_route(client, proc, route, "battle-play")


def scenario_random(client: DebugClient, seconds: float, rng: random.Random) -> None:
    wait_vi(client, 180, timeout=60.0)
    end = time.time() + seconds
    while time.time() < end:
        action = rng.randrange(10)
        if action < 7:
            pulse(
                client,
                rng.choice(BUTTONS),
                hold_frames=rng.randint(2, 10),
                gap_frames=rng.randint(1, 8),
            )
        else:
            hold_stick(
                client,
                rng.choice([-80, -60, 0, 60, 80]),
                rng.choice([-80, -60, 0, 60, 80]),
                hold_frames=rng.randint(4, 16),
            )


def scan_log(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing log: {path}"]
    text = path.read_text(errors="replace")
    return [pattern for pattern in BAD_LOG_PATTERNS if pattern in text]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=["boot", "attract", "scripted-menu", "registration-quit", "battle-entry", "battle-play", "random"], required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--port", type=int, default=4372)
    parser.add_argument("--turbo", action="store_true")
    parser.add_argument("--volume", type=float, default=0.0)
    args = parser.parse_args(argv)

    if not EXE.exists():
        raise SystemExit(f"missing exe: {EXE}")
    if not ROM.exists():
        raise SystemExit(f"missing ROM: {ROM}")

    tag = f"{args.scenario}_seed{args.seed}"
    err_log = ROOT / f"_pms_fuzz_{tag}.err.log"
    out_log = ROOT / f"_pms_fuzz_{tag}.out.log"
    durable_logs = [
        ROOT / "build" / "last_run_report.txt",
        ROOT / "build" / "last_error.log",
        ROOT / "last_run_report.txt",
        ROOT / "last_error.log",
    ]
    for path in (err_log, out_log, *durable_logs):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    proc = start_runner(args.port, err_log, out_log, args.turbo, args.volume)
    client = DebugClient(args.port)
    failed = False
    try:
        client.connect()
        if args.scenario == "boot":
            scenario_boot(client, args.seconds)
        elif args.scenario == "attract":
            scenario_attract(client, args.seconds)
        elif args.scenario == "scripted-menu":
            scenario_scripted_menu(client, args.seconds)
        elif args.scenario == "registration-quit":
            scenario_registration_quit(client, proc)
        elif args.scenario == "battle-entry":
            scenario_battle_entry(client, proc)
        elif args.scenario == "battle-play":
            scenario_battle_play(client, proc)
        elif args.scenario == "random":
            scenario_random(client, args.seconds, random.Random(args.seed))
        client.command("clear_input")
        final_status = client.command("status")
        print(json.dumps({"final_status": final_status, "err_log": str(err_log)}, sort_keys=True))
    except Exception as exc:
        failed = True
        print(f"FAIL: {exc}", file=sys.stderr)
    finally:
        try:
            client.command("quit")
        except Exception:
            pass
        client.close()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    bad = scan_log(err_log)
    if bad:
        failed = True
        print(f"FAIL: bad log patterns in {err_log}: {', '.join(bad)}", file=sys.stderr)
    if proc.returncode not in (0, None):
        failed = True
        print(f"FAIL: runner exit code {proc.returncode}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
