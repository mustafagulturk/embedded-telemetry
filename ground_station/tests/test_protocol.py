from __future__ import annotations

import random

import pytest

from telemetry_gs.crc16 import crc16_ccitt_false
from telemetry_gs.protocol import (
    HEADER_LEN,
    TYPE_STATS,
    TYPE_TELEMETRY,
    Frame,
    FrameDecoder,
    Mode,
    SeqTracker,
    Stats,
    Telemetry,
    encode_frame,
    encode_message,
)

TEL = Telemetry(
    t_ms=123456, ax=-15, ay=7, az=1002, gx=-123, gy=45, gz=900, temp=2431,
    pressure=99876, alt_raw=12345, alt_filt=-678, mode=int(Mode.DEGRADED), faults=0b0101,
)
STATS = Stats(
    uptime_ms=987654, cpu_load=231, sensor_max_us=812, packer_max_us=95, tx_max_us=410,
    queue_drops=3, i2c_errors=17, reset_cause=1, mode=int(Mode.NORMAL),
)


def make_stream(n: int, start_seq: int = 0) -> tuple[bytes, list[int]]:
    """Alternate telemetry/stats frames with consecutive seq numbers."""
    out = bytearray()
    seqs: list[int] = []
    for i in range(n):
        seq = (start_seq + i) & 0xFFFF
        msg: Telemetry | Stats = STATS if i % 5 == 4 else TEL
        out += encode_message(msg, seq)
        seqs.append(seq)
    return bytes(out), seqs


# --------------------------------------------------------------------- CRC


def test_crc_check_value() -> None:
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_crc_incremental_matches_oneshot() -> None:
    data = bytes(range(200))
    assert crc16_ccitt_false(data[100:], crc16_ccitt_false(data[:100])) == crc16_ccitt_false(data)


def test_crc_empty_is_init() -> None:
    assert crc16_ccitt_false(b"") == 0xFFFF


# ---------------------------------------------------------- encode / decode


def test_frame_layout_matches_spec() -> None:
    frame = encode_message(TEL, 0x1234)
    assert len(frame) == 41
    assert frame[0:2] == b"\xA5\x5A"
    assert frame[2] == 0x01  # ver
    assert frame[3] == TYPE_TELEMETRY
    assert frame[4:6] == b"\x34\x12"  # little-endian seq
    assert frame[6] == 32
    crc = crc16_ccitt_false(frame[2:-2])
    assert frame[-2:] == bytes((crc & 0xFF, crc >> 8))
    assert len(encode_message(STATS, 0)) == 7 + 18 + 2


def test_payload_round_trip() -> None:
    assert Telemetry.from_payload(TEL.to_payload()) == TEL
    assert Stats.from_payload(STATS.to_payload()) == STATS


def test_encode_decode_round_trip() -> None:
    dec = FrameDecoder()
    frames = dec.feed(encode_message(TEL, 7) + encode_message(STATS, 8))
    assert [f.seq for f in frames] == [7, 8]
    assert frames[0].frame_type == TYPE_TELEMETRY and frames[0].message == TEL
    assert frames[1].frame_type == TYPE_STATS and frames[1].message == STATS
    assert dec.counters.crc_errors == 0
    assert dec.pending_bytes == 0


def test_physical_units() -> None:
    assert TEL.accel_g == (-0.015, 0.007, 1.002)
    assert TEL.gyro_dps == (-12.3, 4.5, 90.0)
    assert TEL.temp_c == pytest.approx(24.31)
    assert TEL.alt_raw_m == pytest.approx(123.45)
    assert TEL.alt_filt_m == pytest.approx(-6.78)
    assert TEL.t_s == pytest.approx(123.456)
    assert TEL.mode_name == "DEGRADED"
    assert TEL.fault_names == ["IMU", "QUEUE_OVERFLOW"]
    assert STATS.cpu_load_pct == pytest.approx(23.1)
    assert STATS.reset_cause_name == "WATCHDOG"


def test_extreme_values_round_trip() -> None:
    tel = Telemetry(0xFFFFFFFF, -32768, 32767, 0, -32768, 32767, 0, -32768, 0xFFFFFFFF,
                    -(2**31), 2**31 - 1, 255, 255)
    (frame,) = FrameDecoder().feed(encode_message(tel, 0xFFFF))
    assert frame.message == tel


# ------------------------------------------------------------ streaming


def test_chunked_input_split_at_every_byte_boundary() -> None:
    stream, seqs = make_stream(3)
    for cut in range(len(stream) + 1):
        dec = FrameDecoder()
        frames = dec.feed(stream[:cut]) + dec.feed(stream[cut:])
        assert [f.seq for f in frames] == seqs, f"cut at {cut}"
        assert dec.counters.crc_errors == 0
        assert dec.counters.garbage_bytes == 0


def test_byte_by_byte_feed() -> None:
    stream, seqs = make_stream(10)
    dec = FrameDecoder()
    frames: list[Frame] = []
    for b in stream:
        frames += dec.feed(bytes((b,)))
    assert [f.seq for f in frames] == seqs


def test_random_chunk_sizes() -> None:
    stream, seqs = make_stream(200)
    rng = random.Random(42)
    dec = FrameDecoder()
    frames: list[Frame] = []
    i = 0
    while i < len(stream):
        n = rng.randint(1, 97)
        frames += dec.feed(stream[i : i + n])
        i += n
    assert [f.seq for f in frames] == seqs
    assert dec.seq.lost == 0


def test_garbage_between_frames() -> None:
    a = encode_message(TEL, 1)
    b = encode_message(STATS, 2)
    c = encode_message(TEL, 3)
    junk1 = b"\x00\xFF\xA5\x13garbage\xA5"  # contains a lone sync0
    junk2 = b"\xA5\x5A\x01"  # truncated false header right before a real frame
    data = junk1 + a + b + junk2 + c + b"\xA5"
    dec = FrameDecoder()
    frames = dec.feed(data)
    assert [f.seq for f in frames] == [1, 2, 3]
    assert dec.seq.lost == 0
    assert dec.counters.garbage_bytes == len(junk1) + len(junk2)
    assert dec.pending_bytes == 1  # trailing 0xA5 kept as possible sync start


def test_garbage_before_first_frame_split_everywhere() -> None:
    data = b"\x5A\xA5\xA5\x00\x01" + make_stream(2)[0]
    for cut in range(len(data) + 1):
        dec = FrameDecoder()
        frames = dec.feed(data[:cut]) + dec.feed(data[cut:])
        assert [f.seq for f in frames] == [0, 1], f"cut at {cut}"


def test_corrupted_crc_is_counted_skipped_and_resynced() -> None:
    good1 = encode_message(TEL, 10)
    bad = bytearray(encode_message(TEL, 11))
    bad[20] ^= 0x40  # flip a payload bit
    good2 = encode_message(STATS, 12)
    dec = FrameDecoder()
    frames = dec.feed(good1 + bytes(bad) + good2)
    assert [f.seq for f in frames] == [10, 12]
    assert dec.counters.crc_errors == 1
    assert dec.seq.lost == 1  # seq 11 never arrived intact


def test_corrupted_crc_bytes_themselves() -> None:
    bad = bytearray(encode_message(STATS, 0))
    bad[-1] ^= 0xFF
    dec = FrameDecoder()
    assert dec.feed(bytes(bad) + encode_message(STATS, 1)) != []
    assert dec.counters.crc_errors == 1


def test_bad_length_resyncs() -> None:
    bad = bytearray(encode_message(TEL, 5))
    bad[6] = 200  # wrong len for a telemetry frame
    dec = FrameDecoder()
    frames = dec.feed(bytes(bad) + encode_message(TEL, 6))
    assert [f.seq for f in frames] == [6]
    assert dec.counters.bad_len == 1


def test_bad_version_resyncs() -> None:
    bad = bytearray(encode_message(TEL, 5))
    bad[2] = 0x02
    dec = FrameDecoder()
    frames = dec.feed(bytes(bad) + encode_message(TEL, 6))
    assert [f.seq for f in frames] == [6]
    assert dec.counters.bad_version == 1


def test_unknown_type_with_valid_crc_is_accepted() -> None:
    dec = FrameDecoder()
    frames = dec.feed(encode_frame(0x7F, 1, b"\x01\x02\x03") + encode_message(TEL, 2))
    assert [f.seq for f in frames] == [1, 2]
    assert frames[0].message is None
    assert dec.counters.unknown_type == 1
    assert dec.seq.lost == 0


def test_false_sync_inside_payload_does_not_skip_frames() -> None:
    tel = Telemetry(0xA55A, 0x5AA5, 0, 0, 0, 0, 0, 0, 0xA55AA55A, 0, 0, 0, 0)
    stream = b"".join(encode_message(tel, s) for s in range(5))
    for cut in range(len(stream) + 1):
        dec = FrameDecoder()
        frames = dec.feed(stream[:cut]) + dec.feed(stream[cut:])
        assert [f.seq for f in frames] == list(range(5))


def test_random_corruption_never_crashes_and_recovers() -> None:
    rng = random.Random(7)
    stream, _ = make_stream(500)
    data = bytearray(stream)
    for _ in range(40):
        data[rng.randrange(len(data))] = rng.randrange(256)
    dec = FrameDecoder()
    frames = dec.feed(bytes(data) + encode_message(TEL, 500))
    assert frames[-1].seq == 500
    assert len(frames) >= 500 - 40
    assert dec.seq.received + dec.seq.lost == 501


# ------------------------------------------------------------ loss counting


def test_seq_gaps_counted_as_loss() -> None:
    dec = FrameDecoder()
    frames = dec.feed(b"".join(encode_message(TEL, s) for s in (0, 1, 2, 5, 6, 10)))
    assert [f.lost_before for f in frames] == [0, 0, 0, 2, 0, 3]
    assert dec.seq.received == 6
    assert dec.seq.lost == 5
    assert dec.seq.loss_pct == pytest.approx(100 * 5 / 11)
    assert frames[-1].link.rx_total == 6 and frames[-1].link.lost_total == 5
    assert frames[2].link.lost_total == 0


def test_seq_wraparound_without_loss() -> None:
    stream, seqs = make_stream(10, start_seq=0xFFFA)
    dec = FrameDecoder()
    frames = dec.feed(stream)
    assert [f.seq for f in frames] == seqs
    assert seqs[5] == 0xFFFF and seqs[6] == 0
    assert dec.seq.lost == 0


def test_seq_wraparound_with_loss() -> None:
    dec = FrameDecoder()
    dec.feed(b"".join(encode_message(TEL, s) for s in (0xFFFD, 0xFFFE, 1, 2)))
    # missing: 0xFFFF, 0x0000
    assert dec.seq.lost == 2
    assert dec.seq.received == 4


def test_seq_tracker_duplicates_and_resets() -> None:
    t = SeqTracker()
    for s in (100, 101, 101, 102):
        t.update(s)
    assert t.duplicates == 1 and t.received == 3 and t.lost == 0
    t.update(0)  # target reset: big backwards jump, not 65 433 lost frames
    assert t.resyncs == 1 and t.lost == 0
    t.update(2)
    assert t.lost == 1


def test_empty_tracker_loss_pct() -> None:
    assert SeqTracker().loss_pct == 0.0


def test_header_len_constant() -> None:
    assert HEADER_LEN == 7
