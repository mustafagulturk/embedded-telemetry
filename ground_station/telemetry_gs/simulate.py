"""Flight simulator producing realistic telemetry frames without firmware.

Simulates a small rocket flight (pad -> boost -> coast -> apogee -> drogue ->
main chute -> landed) at 100 Hz plus 1 Hz stats frames on one shared seq
counter. Optional link impairments (loss, corruption, garbage) and a sensor
fault that drives the mode DEGRADED -> FAILSAFE.

Usage::

    python -m telemetry_gs.simulate --out sim.bin --duration 60 --loss 0.01
    python -m telemetry_gs.simulate --tcp-server 3456 --loop
"""

from __future__ import annotations

import argparse
import math
import random
import socket
import sys
import time
from collections.abc import Iterator
from dataclasses import dataclass, replace
from pathlib import Path

from .protocol import Fault, Mode, ResetCause, Stats, Telemetry, encode_message
from .sources import parse_host_port

G = 9.80665
P0 = 101325.0
DT = 0.01  # 100 Hz
STATS_EVERY = 100  # ticks -> 1 Hz


def pressure_from_alt(h_m: float) -> float:
    return P0 * math.pow(1.0 - 2.25577e-5 * h_m, 5.25588)


def alt_from_pressure(p_pa: float) -> float:
    return 44330.0 * (1.0 - math.pow(p_pa / P0, 1.0 / 5.25588))


def _clamp(v: float, lo: int, hi: int) -> int:
    return max(lo, min(hi, int(round(v))))


def _i16(v: float) -> int:
    return _clamp(v, -32768, 32767)


@dataclass(frozen=True)
class SimConfig:
    duration_s: float = 60.0
    seed: int = 1
    launch_s: float = 3.0
    boost_s: float = 2.0
    thrust_accel: float = 40.0  # m/s^2 net of nothing (gravity applied separately)
    drogue_vt: float = 15.0  # terminal descent speed under drogue, m/s
    main_vt: float = 5.0  # terminal descent speed under main chute, m/s
    main_deploy_m: float = 150.0
    loss: float = 0.0  # probability a frame is dropped
    corrupt: float = 0.0  # probability a frame gets one bit flipped
    garbage: float = 0.0  # probability random bytes are inserted before a frame
    fault_at_s: float | None = None
    fault_sensor: str = "imu"  # "imu" or "baro"
    boot_ms: int = 1500  # uptime at the first sample


@dataclass(frozen=True)
class SimFrame:
    t_s: float  # simulation time since start
    data: bytes  # bytes to put on the wire (may be empty if the frame was "lost")


class FlightSimulator:
    def __init__(self, cfg: SimConfig) -> None:
        self.cfg = cfg
        self.rng = random.Random(cfg.seed)

    # -- physics -----------------------------------------------------------
    def _accel(self, t: float, h: float, v: float, deployed_main: bool) -> float:
        """True vertical acceleration (m/s^2, up positive)."""
        c = self.cfg
        if t < c.launch_s:
            return 0.0
        if t < c.launch_s + c.boost_s:
            return c.thrust_accel - G - 0.0004 * v * abs(v)
        if v > 0:
            return -G - 0.0004 * v * v
        vt = c.main_vt if deployed_main else c.drogue_vt
        drag = G * (v / vt) ** 2
        return -G + min(drag, 5.0 * G)

    def frames(self) -> Iterator[SimFrame]:
        c = self.cfg
        rng = self.rng
        h = 0.0
        v = 0.0
        launched = False
        landed = False
        deployed_main = False
        seq = 0
        # alpha-beta altitude filter state (as the firmware would do)
        f_h = 0.0
        f_v = 0.0
        alpha, beta = 0.08, 0.004
        last_alt_raw = 0.0
        fault_since: float | None = None
        i2c_errors = 0
        sensor_max = packer_max = tx_max = 0
        n_ticks = int(round(c.duration_s / DT))

        for tick in range(n_ticks):
            t = tick * DT
            t_ms = c.boot_ms + tick * 10

            # ---- dynamics
            a = 0.0
            if not landed:
                a = self._accel(t, h, v, deployed_main)
                v += a * DT
                h += v * DT
                if t >= c.launch_s:
                    launched = True
                if launched and v < 0 and h <= c.main_deploy_m:
                    deployed_main = True
                if launched and h <= 0.0 and t > c.launch_s + c.boost_s:
                    h, v, a = 0.0, 0.0, 0.0
                    landed = True

            # ---- fault state
            faults = 0
            mode = Mode.NORMAL
            if c.fault_at_s is not None and t >= c.fault_at_s:
                if fault_since is None:
                    fault_since = t
                faults |= Fault.IMU if c.fault_sensor == "imu" else Fault.BARO
                mode = Mode.DEGRADED if t - fault_since < 0.3 else Mode.FAILSAFE
                i2c_errors = min(i2c_errors + 1, 0xFFFF)
            imu_ok = not (faults & Fault.IMU)
            baro_ok = not (faults & Fault.BARO)

            # ---- IMU (specific force along body z = vertical)
            boosting = c.launch_s <= t < c.launch_s + c.boost_s
            vib = 120.0 if boosting else 0.0
            if imu_ok:
                az_mg = (a + G) / G * 1000.0 + rng.gauss(0, 12 + vib)
                ax_mg = rng.gauss(0, 10 + vib * 0.5) + (3.0 if not launched else 0.0)
                ay_mg = rng.gauss(0, 10 + vib * 0.5) - 5.0
                swing = 12.0 * math.sin(2 * math.pi * 0.4 * t) if (launched and v < 0) else 0.0
                spin = 90.0 * min(1.0, (t - c.launch_s) / c.boost_s) if launched and v > 0 else 0.0
                gx = swing + rng.gauss(0, 0.4)
                gy = 0.6 * swing + rng.gauss(0, 0.4)
                gz = spin + rng.gauss(0, 0.4)
            else:
                ax_mg = ay_mg = az_mg = 0.0
                gx = gy = gz = 0.0

            # ---- barometer
            temp_c = 24.5 - 0.0065 * h + rng.gauss(0, 0.02)
            if baro_ok:
                pressure = pressure_from_alt(h) + rng.gauss(0, 6.0)
                alt_raw = alt_from_pressure(round(pressure))
                last_alt_raw = alt_raw
            else:
                pressure = 0.0
                alt_raw = last_alt_raw
            if baro_ok:
                pred = f_h + f_v * DT
                r = alt_raw - pred
                f_h = pred + alpha * r
                f_v = f_v + beta * r / DT

            tel = Telemetry(
                t_ms=t_ms & 0xFFFFFFFF,
                ax=_i16(ax_mg), ay=_i16(ay_mg), az=_i16(az_mg),
                gx=_i16(gx * 10), gy=_i16(gy * 10), gz=_i16(gz * 10),
                temp=_i16(temp_c * 100),
                pressure=_clamp(pressure, 0, 0xFFFFFFFF),
                alt_raw=_clamp(alt_raw * 100, -(2**31), 2**31 - 1),
                alt_filt=_clamp(f_h * 100, -(2**31), 2**31 - 1),
                mode=int(mode), faults=int(faults),
            )
            yield SimFrame(t, self._impair(encode_message(tel, seq)))
            seq = (seq + 1) & 0xFFFF

            # ---- 1 Hz stats on the same seq counter
            sensor_max = max(sensor_max, int(rng.gauss(640, 25)) + (300 if not imu_ok else 0))
            packer_max = max(packer_max, int(rng.gauss(85, 6)))
            tx_max = max(tx_max, int(rng.gauss(370, 15)))
            if tick % STATS_EVERY == STATS_EVERY - 1:
                st = Stats(
                    uptime_ms=t_ms & 0xFFFFFFFF,
                    cpu_load=_clamp(rng.gauss(185 if imu_ok else 230, 12), 0, 1000),
                    sensor_max_us=min(sensor_max, 0xFFFF),
                    packer_max_us=min(packer_max, 0xFFFF),
                    tx_max_us=min(tx_max, 0xFFFF),
                    queue_drops=0,
                    i2c_errors=i2c_errors,
                    reset_cause=int(ResetCause.POWER_ON),
                    mode=int(mode),
                )
                yield SimFrame(t, self._impair(encode_message(st, seq)))
                seq = (seq + 1) & 0xFFFF

    # -- link impairments ----------------------------------------------------
    def _impair(self, frame: bytes) -> bytes:
        c = self.cfg
        rng = self.rng
        if c.loss > 0 and rng.random() < c.loss:
            return b""
        out = bytearray(frame)
        if c.corrupt > 0 and rng.random() < c.corrupt:
            idx = rng.randrange(2, len(out))  # keep sync so the CRC check is exercised
            out[idx] ^= 1 << rng.randrange(8)
        if c.garbage > 0 and rng.random() < c.garbage:
            junk = bytes(rng.randrange(256) for _ in range(rng.randint(1, 24)))
            out[0:0] = junk
        return bytes(out)


def generate_bytes(cfg: SimConfig) -> bytes:
    return b"".join(f.data for f in FlightSimulator(cfg).frames())


def write_file(cfg: SimConfig, path: Path) -> int:
    data = generate_bytes(cfg)
    path.write_bytes(data)
    return len(data)


def serve_tcp(cfg: SimConfig, host: str, port: int, speed: float, loop: bool) -> None:
    """Stream frames in (scaled) real time to one TCP client at a time."""
    with socket.create_server((host, port)) as srv:
        print(f"Simülatör dinliyor: {host}:{port} (Ctrl+C ile çık)", file=sys.stderr)
        while True:
            conn, addr = srv.accept()
            print(f"İstemci bağlandı: {addr[0]}:{addr[1]}", file=sys.stderr)
            run = 0
            try:
                with conn:
                    while True:
                        sim = FlightSimulator(replace(cfg, seed=cfg.seed + run))
                        _stream(sim, conn, speed)
                        run += 1
                        if not loop:
                            break
            except (BrokenPipeError, ConnectionResetError):
                print("İstemci bağlantıyı kapattı.", file=sys.stderr)
            if not loop:
                return


def _stream(sim: FlightSimulator, conn: socket.socket, speed: float) -> None:
    start = time.monotonic()
    pending = bytearray()
    for frame in sim.frames():
        pending += frame.data
        target = start + frame.t_s / speed
        now = time.monotonic()
        if target > now:
            if pending:
                conn.sendall(pending)
                pending.clear()
            time.sleep(target - now)
    if pending:
        conn.sendall(pending)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        add_help=False,
        prog="python -m telemetry_gs.simulate",
        description="Sahte telemetri üreteci: simüle uçuş profili (bekleme, itki, "
        "süzülme, tepe noktası, paraşüt, iniş) ile ikili çerçeveler üretir.",
    )
    out = p.add_mutually_exclusive_group(required=True)
    out.add_argument("--out", type=Path, metavar="DOSYA",
                     help="çerçeveleri ham ikili dosyaya yaz (tekrar oynatma için)")
    out.add_argument("--tcp-server", metavar="[HOST:]PORT",
                     help="TCP sunucusu olarak gerçek zamanlı yayın yap (örn. 3456)")
    p.add_argument("--duration", type=float, default=60.0, metavar="S",
                   help="uçuş süresi, saniye (varsayılan: 60)")
    p.add_argument("--seed", type=int, default=1, help="rastgele tohum (varsayılan: 1)")
    p.add_argument("--loss", type=float, default=0.0, metavar="P",
                   help="çerçeve kaybı olasılığı 0..1 (örn. 0.01)")
    p.add_argument("--corrupt", type=float, default=0.0, metavar="P",
                   help="çerçevede bit bozulması olasılığı 0..1 (CRC hatası üretir)")
    p.add_argument("--garbage", type=float, default=0.0, metavar="P",
                   help="çerçeveler arasına rastgele çöp bayt ekleme olasılığı 0..1")
    p.add_argument("--fault-at", type=float, default=None, metavar="S",
                   help="bu saniyede sensör arızası enjekte et (DEGRADED -> FAILSAFE)")
    p.add_argument("--fault-sensor", choices=("imu", "baro"), default="imu",
                   help="arızalanacak sensör (varsayılan: imu)")
    p.add_argument("--speed", type=float, default=1.0,
                   help="TCP modunda oynatma hızı çarpanı (varsayılan: 1.0 = gerçek zaman)")
    p.add_argument("--loop", action="store_true",
                   help="TCP modunda uçuşu sürekli tekrarla ve yeni istemcileri kabul et")
    p.add_argument("-h", "--help", action="help", help="bu yardım mesajını göster ve çık")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    cfg = SimConfig(
        duration_s=args.duration,
        seed=args.seed,
        loss=args.loss,
        corrupt=args.corrupt,
        garbage=args.garbage,
        fault_at_s=args.fault_at,
        fault_sensor=args.fault_sensor,
    )
    if args.out is not None:
        n = write_file(cfg, args.out)
        print(f"{args.out}: {n} bayt yazıldı ({cfg.duration_s:.0f} s uçuş)", file=sys.stderr)
        return 0
    host, port = parse_host_port(args.tcp_server, default_host="127.0.0.1")
    try:
        serve_tcp(cfg, host, port, args.speed, args.loop)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
