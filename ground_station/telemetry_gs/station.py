"""Ground station core: pulls bytes from a source, decodes, logs and keeps plot history."""

from __future__ import annotations

import time
from collections import deque
from typing import BinaryIO

from .csv_logger import CsvLogger
from .protocol import Frame, FrameDecoder, LinkCounters, Stats, Telemetry
from .sources import ByteSource

TELEMETRY_RATE_HZ = 100


class GroundStation:
    def __init__(
        self,
        source: ByteSource,
        csv_logger: CsvLogger | None = None,
        raw_out: BinaryIO | None = None,
        history_s: float = 60.0,
    ) -> None:
        self.source = source
        self.decoder = FrameDecoder()
        self.csv_logger = csv_logger
        self.raw_out = raw_out
        self.history: deque[Telemetry] = deque(maxlen=int(history_s * TELEMETRY_RATE_HZ))
        self.latest_stats: Stats | None = None
        self.bytes_received = 0
        self.start_time = time.monotonic()

    @property
    def latest_telemetry(self) -> Telemetry | None:
        return self.history[-1] if self.history else None

    @property
    def link(self) -> LinkCounters:
        return LinkCounters(
            rx_total=self.decoder.seq.received,
            lost_total=self.decoder.seq.lost,
            crc_errors_total=self.decoder.counters.crc_errors,
        )

    def elapsed(self) -> float:
        return time.monotonic() - self.start_time

    def poll(self, budget_s: float = 0.05) -> int:
        """Read and process data for at most ``budget_s``; return frames decoded.

        Returns early when the source has no data right now or is exhausted.
        """
        deadline = time.monotonic() + budget_s
        count = 0
        while True:
            data = self.source.read()
            if not data:
                break  # nothing available right now, or source exhausted
            count += self.process(data)
            if time.monotonic() >= deadline:
                break
        return count

    def process(self, data: bytes) -> int:
        self.bytes_received += len(data)
        if self.raw_out is not None:
            self.raw_out.write(data)
        frames = self.decoder.feed(data)
        for frame in frames:
            self._handle(frame)
        return len(frames)

    def _handle(self, frame: Frame) -> None:
        msg = frame.message
        now = self.elapsed()
        if isinstance(msg, Telemetry):
            self.history.append(msg)
            if self.csv_logger is not None:
                self.csv_logger.log_telemetry(now, frame.seq, msg, frame.link)
        elif isinstance(msg, Stats):
            self.latest_stats = msg
            if self.csv_logger is not None:
                self.csv_logger.log_stats(now, frame.seq, msg)

    def status_line(self) -> str:
        tel = self.latest_telemetry
        seq = self.decoder.seq
        parts = [
            f"t={self.elapsed():6.1f}s",
            f"alınan={seq.received}",
            f"kayıp={seq.lost} ({seq.loss_pct:.2f}%)",
            f"crc={self.decoder.counters.crc_errors}",
        ]
        if tel is not None:
            parts.append(f"irtifa={tel.alt_filt_m:7.2f}m")
            parts.append(f"mod={tel.mode_name}")
        if self.latest_stats is not None:
            parts.append(f"cpu={self.latest_stats.cpu_load_pct:.1f}%")
        return "  ".join(parts)

    def summary_lines(self) -> list[str]:
        return [
            f"Kaynak       : {self.source.name}",
            f"Süre         : {self.elapsed():.1f} s, {self.bytes_received} bayt",
            *self.decoder.summary_lines(),
        ]
