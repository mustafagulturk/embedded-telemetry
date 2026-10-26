"""Command line entry point: ``python -m telemetry_gs``."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import BinaryIO

from .csv_logger import CsvLogger
from .plot import run_live_plot
from .sources import ByteSource, ReplaySource, SerialSource, TcpSource, parse_host_port
from .station import GroundStation

REALTIME_RATE = 4127.0  # bytes/s: 100 Hz x 41 B telemetry + 1 Hz x 27 B stats


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        add_help=False,
        prog="python -m telemetry_gs",
        description="Gömülü telemetri kartı için yer istasyonu: ikili çerçeveleri çözer, "
        "CSV'ye kaydeder, canlı grafik çizer ve kayıp/CRC istatistiği verir.",
        epilog="Örnekler:\n"
        "  python -m telemetry_gs --serial /dev/ttyACM0 --baud 115200 --csv ucus.csv\n"
        "  python -m telemetry_gs --tcp localhost:3456\n"
        "  python -m telemetry_gs --replay kayit.bin --no-plot --csv kayit.csv",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--serial", metavar="PORT", help="seri port (örn. /dev/ttyACM0, COM3)")
    src.add_argument("--tcp", metavar="HOST:PORT",
                     help="TCP istemcisi (örn. Renode UART soket terminali localhost:3456)")
    src.add_argument("--replay", type=Path, metavar="DOSYA",
                     help="ham ikili kayıt dosyasını tekrar oynat")
    p.add_argument("--baud", type=int, default=115200, help="seri port hızı (varsayılan: 115200)")
    p.add_argument("--replay-rate", type=float, default=None, metavar="B/s",
                   help="tekrar oynatma hızı, bayt/s (0 = sınırsız). Varsayılan: grafikle "
                   "4127 (≈ gerçek zaman), --no-plot ile sınırsız")
    p.add_argument("--connect-timeout", type=float, default=10.0, metavar="S",
                   help="TCP bağlantısı için bekleme süresi (varsayılan: 10)")
    p.add_argument("--csv", type=Path, metavar="DOSYA", help="telemetriyi CSV dosyasına yaz")
    p.add_argument("--stats-csv", type=Path, metavar="DOSYA",
                   help="istatistik (1 Hz) çerçevelerini ayrı CSV dosyasına yaz")
    p.add_argument("--raw-out", type=Path, metavar="DOSYA",
                   help="alınan ham baytları dosyaya kaydet (sonra --replay ile oynatılabilir)")
    p.add_argument("--no-plot", action="store_true", help="grafik penceresi açma (başsız mod)")
    p.add_argument("--duration", type=float, default=None, metavar="S",
                   help="bu kadar saniye sonra dur")
    p.add_argument("--window", type=float, default=10.0, metavar="S",
                   help="grafikte gösterilen zaman penceresi (varsayılan: 10 s)")
    p.add_argument("--quiet", action="store_true",
                   help="başsız modda saniyelik durum satırını yazdırma")
    p.add_argument("-h", "--help", action="help", help="bu yardım mesajını göster ve çık")
    return p


def open_source(args: argparse.Namespace) -> ByteSource:
    if args.serial:
        return SerialSource(args.serial, args.baud)
    if args.tcp:
        host, port = parse_host_port(args.tcp)
        return TcpSource(host, port, connect_timeout_s=args.connect_timeout)
    rate = args.replay_rate
    if rate is None:
        rate = 0.0 if args.no_plot else REALTIME_RATE
    return ReplaySource(args.replay, rate_bps=rate)


def run_headless(station: GroundStation, duration_s: float | None, quiet: bool) -> None:
    next_status = time.monotonic() + 1.0
    while True:
        station.poll(budget_s=0.1)
        if station.source.eof:
            break
        if duration_s is not None and station.elapsed() >= duration_s:
            break
        if not quiet and time.monotonic() >= next_status:
            print(station.status_line(), file=sys.stderr)
            next_status += 1.0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        source = open_source(args)
    except (OSError, ConnectionError) as exc:
        print(f"Hata: kaynak açılamadı: {exc}", file=sys.stderr)
        return 2

    csv_logger = CsvLogger(args.csv, args.stats_csv) if (args.csv or args.stats_csv) else None
    raw_out: BinaryIO | None = open(args.raw_out, "wb") if args.raw_out else None
    station = GroundStation(source, csv_logger=csv_logger, raw_out=raw_out)

    try:
        if args.no_plot:
            run_headless(station, args.duration, args.quiet)
        else:
            try:
                run_live_plot(station, window_s=args.window, duration_s=args.duration)
            except RuntimeError as exc:
                print(f"Uyarı: grafik açılamadı ({exc}); --no-plot moduna geçiliyor.",
                      file=sys.stderr)
                run_headless(station, args.duration, args.quiet)
    except KeyboardInterrupt:
        pass
    finally:
        source.close()
        if csv_logger is not None:
            csv_logger.close()
        if raw_out is not None:
            raw_out.close()

    print("\n=== Bağlantı özeti ===", file=sys.stderr)
    for line in station.summary_lines():
        print(line, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
