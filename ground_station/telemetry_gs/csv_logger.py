"""CSV logging of decoded telemetry and stats.

Column names carry their units. Values are written with exactly the precision of
the wire format, so a CSV row converts back to the original raw integers
(see :func:`read_telemetry_csv` / :func:`read_stats_csv`).
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO

from .protocol import LinkCounters, Stats, Telemetry, mode_name

TELEMETRY_COLUMNS: tuple[str, ...] = (
    "host_time_s",
    "seq",
    "t_s",
    "ax_g",
    "ay_g",
    "az_g",
    "gx_dps",
    "gy_dps",
    "gz_dps",
    "temp_c",
    "pressure_pa",
    "alt_raw_m",
    "alt_filt_m",
    "mode",
    "mode_name",
    "faults",
    "rx_total",
    "lost_total",
    "crc_errors_total",
)

STATS_COLUMNS: tuple[str, ...] = (
    "host_time_s",
    "seq",
    "uptime_s",
    "cpu_load_pct",
    "sensor_max_us",
    "packer_max_us",
    "tx_max_us",
    "queue_drops",
    "i2c_errors",
    "reset_cause",
    "mode",
    "mode_name",
)


@dataclass(frozen=True)
class TelemetryRow:
    host_time_s: float
    seq: int
    telemetry: Telemetry
    link: LinkCounters


@dataclass(frozen=True)
class StatsRow:
    host_time_s: float
    seq: int
    stats: Stats


class CsvLogger:
    """Writes telemetry rows to ``telemetry_path`` and stats rows to ``stats_path``.

    Either path may be None to skip that file.
    """

    def __init__(
        self, telemetry_path: str | Path | None, stats_path: str | Path | None = None
    ) -> None:
        self._tel_file: TextIO | None = (
            open(telemetry_path, "w", newline="", encoding="utf-8")
            if telemetry_path is not None
            else None
        )
        self._stats_file: TextIO | None = (
            open(stats_path, "w", newline="", encoding="utf-8") if stats_path is not None else None
        )
        self._tel = csv.writer(self._tel_file) if self._tel_file is not None else None
        self._stats = csv.writer(self._stats_file) if self._stats_file is not None else None
        if self._tel is not None:
            self._tel.writerow(TELEMETRY_COLUMNS)
        if self._stats is not None:
            self._stats.writerow(STATS_COLUMNS)

    def log_telemetry(
        self, host_time_s: float, seq: int, tel: Telemetry, link: LinkCounters
    ) -> None:
        if self._tel is None:
            return
        self._tel.writerow(
            (
                f"{host_time_s:.3f}",
                seq,
                f"{tel.t_ms / 1000:.3f}",
                f"{tel.ax / 1000:.3f}",
                f"{tel.ay / 1000:.3f}",
                f"{tel.az / 1000:.3f}",
                f"{tel.gx / 10:.1f}",
                f"{tel.gy / 10:.1f}",
                f"{tel.gz / 10:.1f}",
                f"{tel.temp / 100:.2f}",
                tel.pressure,
                f"{tel.alt_raw / 100:.2f}",
                f"{tel.alt_filt / 100:.2f}",
                tel.mode,
                mode_name(tel.mode),
                tel.faults,
                link.rx_total,
                link.lost_total,
                link.crc_errors_total,
            )
        )

    def log_stats(self, host_time_s: float, seq: int, st: Stats) -> None:
        if self._stats is None:
            return
        self._stats.writerow(
            (
                f"{host_time_s:.3f}",
                seq,
                f"{st.uptime_ms / 1000:.3f}",
                f"{st.cpu_load / 10:.1f}",
                st.sensor_max_us,
                st.packer_max_us,
                st.tx_max_us,
                st.queue_drops,
                st.i2c_errors,
                st.reset_cause,
                st.mode,
                mode_name(st.mode),
            )
        )

    def _files(self) -> list[TextIO]:
        return [f for f in (self._tel_file, self._stats_file) if f is not None]

    def flush(self) -> None:
        for f in self._files():
            f.flush()

    def close(self) -> None:
        for f in self._files():
            f.close()

    def __enter__(self) -> CsvLogger:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()


def _scaled(text: str, scale: int) -> int:
    return round(float(text) * scale)


def read_telemetry_csv(path: str | Path) -> list[TelemetryRow]:
    rows: list[TelemetryRow] = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            tel = Telemetry(
                t_ms=_scaled(r["t_s"], 1000),
                ax=_scaled(r["ax_g"], 1000),
                ay=_scaled(r["ay_g"], 1000),
                az=_scaled(r["az_g"], 1000),
                gx=_scaled(r["gx_dps"], 10),
                gy=_scaled(r["gy_dps"], 10),
                gz=_scaled(r["gz_dps"], 10),
                temp=_scaled(r["temp_c"], 100),
                pressure=int(r["pressure_pa"]),
                alt_raw=_scaled(r["alt_raw_m"], 100),
                alt_filt=_scaled(r["alt_filt_m"], 100),
                mode=int(r["mode"]),
                faults=int(r["faults"]),
            )
            link = LinkCounters(
                int(r["rx_total"]), int(r["lost_total"]), int(r["crc_errors_total"])
            )
            rows.append(TelemetryRow(float(r["host_time_s"]), int(r["seq"]), tel, link))
    return rows


def read_stats_csv(path: str | Path) -> list[StatsRow]:
    rows: list[StatsRow] = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            st = Stats(
                uptime_ms=_scaled(r["uptime_s"], 1000),
                cpu_load=_scaled(r["cpu_load_pct"], 10),
                sensor_max_us=int(r["sensor_max_us"]),
                packer_max_us=int(r["packer_max_us"]),
                tx_max_us=int(r["tx_max_us"]),
                queue_drops=int(r["queue_drops"]),
                i2c_errors=int(r["i2c_errors"]),
                reset_cause=int(r["reset_cause"]),
                mode=int(r["mode"]),
            )
            rows.append(StatsRow(float(r["host_time_s"]), int(r["seq"]), st))
    return rows
