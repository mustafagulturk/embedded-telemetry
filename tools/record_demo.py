#!/usr/bin/env python3
"""Record a deterministic Renode demo flight and turn it into CSV + GIF.

Scenario (virtual time): 2 s on the ground, climb to 120 m, hover, descend.
At 20 s the MPU6050 is disconnected (-> DEGRADED), reconnected at 25 s;
at 32 s the BMP280 is disconnected (-> FAILSAFE), reconnected at 35 s.

Usage (repo root, firmware built into build/fw):
    python3 tools/record_demo.py --renode /path/to/renode --out build/demo
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCENARIO = [
    ("20", "sysbus.i2c1.mpu6050 Disconnected true"),
    ("5", "sysbus.i2c1.mpu6050 Disconnected false"),
    ("7", "sysbus.i2c1.bmp280 Disconnected true"),
    ("3", "sysbus.i2c1.bmp280 Disconnected false"),
    ("13", None),
]


def capture(port: int, out: Path, stop: threading.Event) -> None:
    deadline = time.time() + 120
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            break
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(0.2)
    sock.settimeout(0.5)
    with out.open("wb") as f:
        while not stop.is_set():
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            except ConnectionResetError:
                break  # Renode closed the socket on quit
            if not data:
                break
            f.write(data)
    sock.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--renode", default="renode")
    ap.add_argument("--elf", default=str(ROOT / "build/fw/firmware/firmware.elf"))
    ap.add_argument("--out", default=str(ROOT / "build/demo"))
    ap.add_argument("--port", type=int, default=3456)
    ap.add_argument("--noise-pa", type=float, default=6.0,
                    help="BMP280 pressure noise (Pa); ~6 Pa is ~0.5 m, makes the filter visible")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    raw = out / "renode_flight.bin"

    cmds = [
        f"$elf=@{args.elf}",
        f"$uart_port={args.port}",
        f"include @{ROOT / 'renode/telemetry.resc'}",
        f"sysbus.i2c1.bmp280 PressureNoiseStdDev {args.noise_pa}",
    ]
    for seconds, action in SCENARIO:
        cmds.append(f'emulation RunFor "{seconds}"')
        if action:
            cmds.append(action)
    cmds.append("quit")

    stop = threading.Event()
    t = threading.Thread(target=capture, args=(args.port, raw, stop), daemon=True)
    t.start()
    proc = subprocess.run(
        [args.renode, "--disable-gui", "--console", "-e", "; ".join(cmds)],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, check=False,
    )
    time.sleep(1.0)
    stop.set()
    t.join(timeout=5)
    print(f"renode exit={proc.returncode}, captured {raw.stat().st_size} bytes -> {raw}")

    gs = ROOT / "ground_station"
    csv = out / "renode_flight.csv"
    stats = out / "renode_flight_stats.csv"
    subprocess.run([sys.executable, "-m", "telemetry_gs", "--replay", str(raw), "--no-plot",
                    "--csv", str(csv), "--stats-csv", str(stats)], cwd=gs, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
