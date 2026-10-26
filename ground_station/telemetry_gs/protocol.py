"""Binary telemetry frame format (docs/packet_format.md, v1) and streaming decoder.

Frame layout (all multi-byte fields little-endian)::

    0xA5 0x5A | ver u8 | type u8 | seq u16 | len u8 | payload[len] | crc u16

The CRC (CRC-16/CCITT-FALSE) covers ``ver`` .. end of payload.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from enum import IntEnum, IntFlag
from typing import ClassVar, Union

from .crc16 import crc16_ccitt_false

SYNC0 = 0xA5
SYNC1 = 0x5A
SYNC = bytes((SYNC0, SYNC1))
PROTOCOL_VERSION = 0x01

TYPE_TELEMETRY = 0x01
TYPE_STATS = 0x02

HEADER_LEN = 7  # sync0, sync1, ver, type, seq(2), len
CRC_LEN = 2
SEQ_MOD = 1 << 16

_HEADER = struct.Struct("<BBBBHB")
_CRC = struct.Struct("<H")


class Mode(IntEnum):
    NORMAL = 0
    DEGRADED = 1
    FAILSAFE = 2


class Fault(IntFlag):
    IMU = 0x01
    BARO = 0x02
    QUEUE_OVERFLOW = 0x04
    WATCHDOG_RESET = 0x08


class ResetCause(IntEnum):
    POWER_ON = 0
    WATCHDOG = 1
    SOFTWARE = 2
    OTHER = 3


def mode_name(mode: int) -> str:
    try:
        return Mode(mode).name
    except ValueError:
        return f"UNKNOWN({mode})"


def fault_names(faults: int) -> list[str]:
    """Names of the set fault bits; unknown bits are reported as ``BITn``."""
    names: list[str] = []
    for bit in range(8):
        mask = 1 << bit
        if faults & mask:
            try:
                names.append(Fault(mask).name or f"BIT{bit}")
            except ValueError:
                names.append(f"BIT{bit}")
    return names


def reset_cause_name(cause: int) -> str:
    try:
        return ResetCause(cause).name
    except ValueError:
        return f"UNKNOWN({cause})"


# ---------------------------------------------------------------------------
# Payloads
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Telemetry:
    """Type 0x01 payload (32 bytes). Fields hold the raw integer wire values."""

    t_ms: int
    ax: int  # mg
    ay: int
    az: int
    gx: int  # 0.1 deg/s
    gy: int
    gz: int
    temp: int  # 0.01 degC
    pressure: int  # Pa
    alt_raw: int  # cm
    alt_filt: int  # cm
    mode: int
    faults: int

    STRUCT: ClassVar[struct.Struct] = struct.Struct("<I6hhIiiBB")
    SIZE: ClassVar[int] = 32

    @classmethod
    def from_payload(cls, payload: bytes | bytearray | memoryview) -> Telemetry:
        return cls(*cls.STRUCT.unpack(payload))

    def to_payload(self) -> bytes:
        return self.STRUCT.pack(
            self.t_ms, self.ax, self.ay, self.az, self.gx, self.gy, self.gz,
            self.temp, self.pressure, self.alt_raw, self.alt_filt, self.mode, self.faults,
        )

    # Physical-unit views -------------------------------------------------
    @property
    def t_s(self) -> float:
        return self.t_ms / 1000.0

    @property
    def accel_g(self) -> tuple[float, float, float]:
        return (self.ax / 1000.0, self.ay / 1000.0, self.az / 1000.0)

    @property
    def gyro_dps(self) -> tuple[float, float, float]:
        return (self.gx / 10.0, self.gy / 10.0, self.gz / 10.0)

    @property
    def temp_c(self) -> float:
        return self.temp / 100.0

    @property
    def pressure_pa(self) -> int:
        return self.pressure

    @property
    def alt_raw_m(self) -> float:
        return self.alt_raw / 100.0

    @property
    def alt_filt_m(self) -> float:
        return self.alt_filt / 100.0

    @property
    def mode_name(self) -> str:
        return mode_name(self.mode)

    @property
    def fault_names(self) -> list[str]:
        return fault_names(self.faults)


@dataclass(frozen=True)
class Stats:
    """Type 0x02 payload (18 bytes). Fields hold the raw integer wire values."""

    uptime_ms: int
    cpu_load: int  # per mille
    sensor_max_us: int
    packer_max_us: int
    tx_max_us: int
    queue_drops: int
    i2c_errors: int
    reset_cause: int
    mode: int

    STRUCT: ClassVar[struct.Struct] = struct.Struct("<I6HBB")
    SIZE: ClassVar[int] = 18

    @classmethod
    def from_payload(cls, payload: bytes | bytearray | memoryview) -> Stats:
        return cls(*cls.STRUCT.unpack(payload))

    def to_payload(self) -> bytes:
        return self.STRUCT.pack(
            self.uptime_ms, self.cpu_load, self.sensor_max_us, self.packer_max_us,
            self.tx_max_us, self.queue_drops, self.i2c_errors, self.reset_cause, self.mode,
        )

    @property
    def uptime_s(self) -> float:
        return self.uptime_ms / 1000.0

    @property
    def cpu_load_pct(self) -> float:
        return self.cpu_load / 10.0

    @property
    def mode_name(self) -> str:
        return mode_name(self.mode)

    @property
    def reset_cause_name(self) -> str:
        return reset_cause_name(self.reset_cause)


Message = Union[Telemetry, Stats]

PAYLOAD_SIZES: dict[int, int] = {TYPE_TELEMETRY: Telemetry.SIZE, TYPE_STATS: Stats.SIZE}

assert Telemetry.STRUCT.size == Telemetry.SIZE
assert Stats.STRUCT.size == Stats.SIZE


# ---------------------------------------------------------------------------
# Encoding
# ---------------------------------------------------------------------------


def encode_frame(
    frame_type: int, seq: int, payload: bytes, version: int = PROTOCOL_VERSION
) -> bytes:
    """Build a complete frame (sync .. crc) for ``payload``."""
    if len(payload) > 0xFF:
        raise ValueError("payload longer than 255 bytes")
    header = _HEADER.pack(SYNC0, SYNC1, version, frame_type, seq % SEQ_MOD, len(payload))
    crc = crc16_ccitt_false(header[2:] + payload)
    return header + payload + _CRC.pack(crc)


def encode_message(msg: Message, seq: int) -> bytes:
    """Encode a Telemetry or Stats message as a frame."""
    if isinstance(msg, Telemetry):
        return encode_frame(TYPE_TELEMETRY, seq, msg.to_payload())
    return encode_frame(TYPE_STATS, seq, msg.to_payload())


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class LinkCounters:
    """Link-quality counters at the time a row was logged."""

    rx_total: int = 0
    lost_total: int = 0
    crc_errors_total: int = 0

    @property
    def loss_pct(self) -> float:
        total = self.rx_total + self.lost_total
        return 100.0 * self.lost_total / total if total else 0.0


@dataclass(frozen=True)
class Frame:
    """A CRC-valid frame. ``message`` is None for unknown frame types."""

    frame_type: int
    seq: int
    payload: bytes
    message: Message | None
    lost_before: int = 0  # frames missing (by seq) right before this one
    link: LinkCounters = field(default_factory=LinkCounters)  # counters right after this frame was decoded


class SeqTracker:
    """Counts packet loss from the shared u16 sequence counter.

    A forward jump of ``d`` (mod 2**16) means ``d - 1`` frames were lost.
    A jump larger than ``max_gap`` (e.g. going backwards after a target reset)
    is counted as a resync instead of loss. A repeated seq is a duplicate.
    """

    def __init__(self, max_gap: int = 0x8000) -> None:
        self.max_gap = max_gap
        self.last: int | None = None
        self.received = 0
        self.lost = 0
        self.duplicates = 0
        self.resyncs = 0

    def update(self, seq: int) -> int:
        """Register ``seq``; return the number of frames lost just before it."""
        if self.last is None:
            self.last = seq
            self.received = 1
            return 0
        delta = (seq - self.last) % SEQ_MOD
        if delta == 0:
            self.duplicates += 1
            return 0
        self.received += 1
        self.last = seq
        if delta > self.max_gap:
            self.resyncs += 1
            return 0
        gap = delta - 1
        self.lost += gap
        return gap

    @property
    def expected(self) -> int:
        return self.received + self.lost

    @property
    def loss_pct(self) -> float:
        total = self.expected
        return 100.0 * self.lost / total if total else 0.0


@dataclass
class DecoderCounters:
    frames: int = 0
    telemetry: int = 0
    stats: int = 0
    unknown_type: int = 0
    crc_errors: int = 0
    bad_len: int = 0
    bad_version: int = 0
    garbage_bytes: int = 0


@dataclass
class FrameDecoder:
    """Streaming frame decoder.

    Feed it arbitrary byte chunks with :meth:`feed`; it returns every complete,
    CRC-valid frame found so far. On a bad version, bad length or CRC mismatch
    it drops one byte and searches for the next sync pattern, so a valid frame
    hidden behind a false sync is never skipped.
    """

    seq: SeqTracker = field(default_factory=SeqTracker)
    counters: DecoderCounters = field(default_factory=DecoderCounters)
    _buf: bytearray = field(default_factory=bytearray, repr=False)

    def feed(self, data: bytes | bytearray | memoryview) -> list[Frame]:
        buf = self._buf
        buf += data
        frames: list[Frame] = []
        c = self.counters
        pos = 0
        n = len(buf)
        while True:
            idx = buf.find(SYNC, pos)
            if idx < 0:
                # Keep a trailing 0xA5 that might be the start of the next sync.
                keep = n - 1 if n > pos and buf[n - 1] == SYNC0 else n
                c.garbage_bytes += keep - pos
                pos = keep
                break
            c.garbage_bytes += idx - pos
            pos = idx
            if n - pos < HEADER_LEN:
                break
            _, _, ver, ftype, seq, length = _HEADER.unpack_from(buf, pos)
            if ver != PROTOCOL_VERSION:
                c.bad_version += 1
                c.garbage_bytes += 1
                pos += 1
                continue
            expected = PAYLOAD_SIZES.get(ftype)
            if expected is not None and length != expected:
                c.bad_len += 1
                c.garbage_bytes += 1
                pos += 1
                continue
            total = HEADER_LEN + length + CRC_LEN
            if n - pos < total:
                break
            body_end = pos + HEADER_LEN + length
            (crc_rx,) = _CRC.unpack_from(buf, body_end)
            if crc16_ccitt_false(buf[pos + 2 : body_end]) != crc_rx:
                c.crc_errors += 1
                c.garbage_bytes += 1
                pos += 1
                continue
            payload = bytes(buf[pos + HEADER_LEN : body_end])
            pos += total
            frames.append(self._make_frame(ftype, seq, payload))
        del buf[:pos]
        return frames

    def _make_frame(self, ftype: int, seq: int, payload: bytes) -> Frame:
        c = self.counters
        c.frames += 1
        message: Message | None
        if ftype == TYPE_TELEMETRY:
            message = Telemetry.from_payload(payload)
            c.telemetry += 1
        elif ftype == TYPE_STATS:
            message = Stats.from_payload(payload)
            c.stats += 1
        else:
            message = None
            c.unknown_type += 1
        lost = self.seq.update(seq)
        link = LinkCounters(self.seq.received, self.seq.lost, c.crc_errors)
        return Frame(ftype, seq, payload, message, lost, link)

    @property
    def pending_bytes(self) -> int:
        return len(self._buf)

    def summary_lines(self) -> list[str]:
        """Human readable (Turkish) summary of link quality."""
        c = self.counters
        s = self.seq
        return [
            f"Çerçeve      : {c.frames} (telemetri {c.telemetry}, istatistik {c.stats}, "
            f"bilinmeyen tip {c.unknown_type})",
            f"Alınan       : {s.received}  Kayıp: {s.lost}  Kayıp oranı: {s.loss_pct:.2f} %",
            f"CRC hatası   : {c.crc_errors}  Uzunluk hatası: {c.bad_len}  "
            f"Sürüm hatası: {c.bad_version}  Atılan bayt: {c.garbage_bytes}",
            f"Yinelenen seq: {s.duplicates}  Sayaç sıçraması (reset?): {s.resyncs}",
        ]
