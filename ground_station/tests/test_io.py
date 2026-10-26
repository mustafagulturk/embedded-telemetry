from __future__ import annotations

import csv
import io
import socket
import threading
from pathlib import Path

from telemetry_gs.__main__ import main as gs_main
from telemetry_gs.csv_logger import (
    STATS_COLUMNS,
    TELEMETRY_COLUMNS,
    CsvLogger,
    read_stats_csv,
    read_telemetry_csv,
)
from telemetry_gs.protocol import FrameDecoder, LinkCounters, Mode, Stats, Telemetry, encode_message
from telemetry_gs.simulate import SimConfig, generate_bytes
from telemetry_gs.sources import ReplaySource, TcpSource, parse_host_port
from telemetry_gs.station import GroundStation

TEL = Telemetry(5000, -15, 7, 1002, -123, 45, 900, 2431, 99876, 12345, -678, 1, 0b11)
STATS = Stats(5000, 231, 812, 95, 410, 3, 17, 1, 0)


def test_csv_header_and_values(tmp_path: Path) -> None:
    tel_path = tmp_path / "t.csv"
    st_path = tmp_path / "s.csv"
    with CsvLogger(tel_path, st_path) as log:
        log.log_telemetry(1.5, 42, TEL, LinkCounters(10, 2, 1))
        log.log_stats(1.6, 43, STATS)

    with open(tel_path, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    assert tuple(rows[0]) == TELEMETRY_COLUMNS
    row = dict(zip(rows[0], rows[1]))
    assert row["seq"] == "42"
    assert row["ax_g"] == "-0.015"
    assert row["gx_dps"] == "-12.3"
    assert row["temp_c"] == "24.31"
    assert row["alt_raw_m"] == "123.45"
    assert row["alt_filt_m"] == "-6.78"
    assert row["mode_name"] == "DEGRADED"
    assert row["lost_total"] == "2"

    with open(st_path, newline="", encoding="utf-8") as f:
        srows = list(csv.reader(f))
    assert tuple(srows[0]) == STATS_COLUMNS
    assert dict(zip(srows[0], srows[1]))["cpu_load_pct"] == "23.1"


def test_csv_read_back_is_lossless(tmp_path: Path) -> None:
    p = tmp_path / "t.csv"
    s = tmp_path / "s.csv"
    with CsvLogger(p, s) as log:
        log.log_telemetry(0.0, 1, TEL, LinkCounters(1, 0, 0))
        log.log_stats(0.0, 2, STATS)
    (row,) = read_telemetry_csv(p)
    assert row.telemetry == TEL
    assert row.seq == 1
    (srow,) = read_stats_csv(s)
    assert srow.stats == STATS


def test_station_replay_to_csv(tmp_path: Path) -> None:
    capture = tmp_path / "cap.bin"
    capture.write_bytes(generate_bytes(SimConfig(duration_s=3.0, loss=0.05, seed=3)))
    csv_path = tmp_path / "out.csv"
    raw = io.BytesIO()
    logger = CsvLogger(csv_path)
    station = GroundStation(ReplaySource(capture), csv_logger=logger, raw_out=raw)
    while not station.source.eof:
        station.poll(0.5)
    logger.close()
    assert raw.getvalue() == capture.read_bytes()
    rows = read_telemetry_csv(csv_path)
    assert len(rows) == station.decoder.counters.telemetry
    assert station.decoder.seq.lost > 0
    assert rows[-1].link.lost_total <= station.decoder.seq.lost
    # rx_total must increase row by row (snapshot per frame, not per chunk)
    assert [r.link.rx_total for r in rows] == sorted({r.link.rx_total for r in rows})


def test_cli_headless_replay(tmp_path: Path) -> None:
    capture = tmp_path / "cap.bin"
    capture.write_bytes(generate_bytes(SimConfig(duration_s=2.0, corrupt=0.05, seed=5)))
    csv_path = tmp_path / "out.csv"
    stats_path = tmp_path / "stats.csv"
    raw_path = tmp_path / "raw.bin"
    rc = gs_main(["--replay", str(capture), "--no-plot", "--quiet", "--csv", str(csv_path),
                  "--stats-csv", str(stats_path), "--raw-out", str(raw_path)])
    assert rc == 0
    assert raw_path.read_bytes() == capture.read_bytes()
    assert len(read_telemetry_csv(csv_path)) > 150
    assert len(read_stats_csv(stats_path)) == 2


def test_simulator_fault_reaches_failsafe() -> None:
    dec = FrameDecoder()
    frames = dec.feed(generate_bytes(SimConfig(duration_s=5.0, fault_at_s=2.0)))
    tel = [f.message for f in frames if isinstance(f.message, Telemetry)]
    assert tel[0].mode == Mode.NORMAL
    assert tel[-1].mode == Mode.FAILSAFE
    assert tel[-1].faults & 0x01
    assert dec.seq.lost == 0 and dec.counters.crc_errors == 0
    assert dec.counters.stats == 5


def test_simulator_flight_profile() -> None:
    frames = FrameDecoder().feed(generate_bytes(SimConfig(duration_s=60.0)))
    tel = [f.message for f in frames if isinstance(f.message, Telemetry)]
    apogee = max(t.alt_filt_m for t in tel)
    assert 100.0 < apogee < 1000.0
    assert abs(tel[-1].alt_filt_m) < 2.0  # landed


def test_parse_host_port() -> None:
    assert parse_host_port("localhost:3456") == ("localhost", 3456)
    assert parse_host_port("3456") == ("localhost", 3456)
    assert parse_host_port(":3456") == ("localhost", 3456)


def test_tcp_source_reads_until_peer_closes() -> None:
    payload = b"".join(encode_message(TEL, s) for s in range(20))
    srv = socket.create_server(("127.0.0.1", 0))
    port = srv.getsockname()[1]

    def serve() -> None:
        conn, _ = srv.accept()
        with conn:
            conn.sendall(payload)
        srv.close()

    th = threading.Thread(target=serve)
    th.start()
    src = TcpSource("127.0.0.1", port, connect_timeout_s=2.0)
    got = bytearray()
    while not src.eof:
        got += src.read()
    src.close()
    th.join()
    assert bytes(got) == payload


def test_live_plot_update_headless(tmp_path: Path) -> None:
    from matplotlib.backends.backend_agg import FigureCanvasAgg
    from matplotlib.figure import Figure

    from telemetry_gs.plot import LivePlot

    capture = tmp_path / "cap.bin"
    capture.write_bytes(generate_bytes(SimConfig(duration_s=4.0, fault_at_s=3.0)))
    station = GroundStation(ReplaySource(capture))
    fig = Figure(figsize=(11, 6.5))
    FigureCanvasAgg(fig)
    live = LivePlot(station, fig, window_s=10.0, poll_budget_s=1.0)
    for i in range(5):
        live.update(i)
    fig.savefig(tmp_path / "live.png")
    assert station.source.eof
    assert "FAILSAFE" in live.dashboard.mode_badge.get_text()
    assert "CPU yükü" in live.dashboard.status.get_text()
