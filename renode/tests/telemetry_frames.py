"""Decoder for the v1 binary telemetry frames (docs/packet_format.md).

Used two ways:
  * as a Robot Framework keyword library by renode/tests/telemetry.robot
    (it connects to the USART2 server-socket terminal that the test creates), and
  * from the command line to decode a raw capture:  python3 telemetry_frames.py capture.bin
"""
import socket
import struct
import sys
import time

SYNC = b"\xA5\x5A"
TELEMETRY_FMT = "<IhhhhhhhIiiBB"   # 32 bytes
STATS_FMT = "<IHHHHHHBB"           # 18 bytes
MODE_NAMES = {0: "NORMAL", 1: "DEGRADED", 2: "FAILSAFE"}


def crc16_ccitt_false(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


assert crc16_ccitt_false(b"123456789") == 0x29B1


def parse_frames(buf):
    """Returns (frames, bad_crc_count, rest). Each frame is a dict."""
    frames, bad, i = [], 0, 0
    while True:
        j = buf.find(SYNC, i)
        if j < 0 or len(buf) - j < 9:
            return frames, bad, buf[j if j >= 0 else max(len(buf) - 1, 0):]
        ver, typ, seq, ln = buf[j + 2], buf[j + 3], struct.unpack_from("<H", buf, j + 4)[0], buf[j + 6]
        end = j + 7 + ln + 2
        if end > len(buf):
            return frames, bad, buf[j:]
        crc = struct.unpack_from("<H", buf, j + 7 + ln)[0]
        if ver != 1 or crc16_ccitt_false(buf[j + 2:j + 7 + ln]) != crc:
            bad += 1
            i = j + 1
            continue
        payload = buf[j + 7:j + 7 + ln]
        f = {"type": typ, "seq": seq, "len": ln}
        if typ == 1 and ln == 32:
            (f["t_ms"], f["ax"], f["ay"], f["az"], f["gx"], f["gy"], f["gz"], f["temp"],
             f["pressure"], f["alt_raw"], f["alt_filt"], f["mode"], f["faults"]) = struct.unpack(TELEMETRY_FMT, payload)
        elif typ == 2 and ln == 18:
            (f["uptime_ms"], f["cpu_load"], f["sensor_max_us"], f["packer_max_us"], f["tx_max_us"],
             f["queue_drops"], f["i2c_errors"], f["reset_cause"], f["mode"]) = struct.unpack(STATS_FMT, payload)
        frames.append(f)
        i = end


class telemetry_frames(object):
    """Robot Framework library (ROBOT_LIBRARY_SCOPE = SUITE)."""
    ROBOT_LIBRARY_SCOPE = "SUITE"

    def __init__(self):
        self.sock = None
        self.buf = b""
        self.frames = []
        self.bad_crc = 0

    # --- connection -------------------------------------------------------
    def connect_to_uart_socket(self, port=3456, timeout=30):
        deadline = time.time() + float(timeout)
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", int(port)), timeout=2)
                self.sock.setblocking(False)
                return
            except OSError:
                if time.time() > deadline:
                    raise AssertionError("cannot connect to UART socket on port %s" % port)
                time.sleep(0.2)

    def disconnect_from_uart_socket(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def drain_uart_socket(self):
        """Reads everything currently buffered on the socket and decodes it."""
        while self.sock:
            try:
                d = self.sock.recv(65536)
            except (BlockingIOError, socket.timeout):
                break
            if not d:
                break
            self.buf += d
        frames, bad, self.buf = parse_frames(self.buf)
        self.frames.extend(frames)
        self.bad_crc += bad
        return len(frames)

    def clear_frames(self):
        self.drain_uart_socket()
        self.frames = []
        self.bad_crc = 0

    # --- queries / assertions ---------------------------------------------
    def get_telemetry_frames(self):
        self.drain_uart_socket()
        return [f for f in self.frames if f["type"] == 1]

    def get_stats_frames(self):
        self.drain_uart_socket()
        return [f for f in self.frames if f["type"] == 2]

    def telemetry_frame_count_should_be_at_least(self, n):
        c = len(self.get_telemetry_frames())
        if c < int(n):
            raise AssertionError("only %d telemetry frames (expected >= %s), bad CRC: %d" % (c, n, self.bad_crc))
        return c

    def stats_frame_count_should_be_at_least(self, n):
        c = len(self.get_stats_frames())
        if c < int(n):
            raise AssertionError("only %d stats frames (expected >= %s)" % (c, n))
        return c

    def crc_errors_should_be_zero(self):
        self.drain_uart_socket()
        if self.bad_crc:
            raise AssertionError("%d frame(s) with bad CRC" % self.bad_crc)

    def sequence_numbers_should_be_contiguous(self):
        self.drain_uart_socket()
        seqs = [f["seq"] for f in self.frames]
        gaps = [(a, b) for a, b in zip(seqs, seqs[1:]) if (a + 1) & 0xFFFF != b]
        if gaps:
            raise AssertionError("sequence gaps: %s" % gaps[:10])

    def last_telemetry_mode(self):
        t = self.get_telemetry_frames()
        if not t:
            raise AssertionError("no telemetry frames")
        return t[-1]["mode"]

    def last_telemetry_field(self, name):
        t = self.get_telemetry_frames()
        if not t:
            raise AssertionError("no telemetry frames")
        return t[-1][name]

    def last_stats_field(self, name):
        s = self.get_stats_frames()
        if not s:
            raise AssertionError("no stats frames")
        return s[-1][name]

    def all_telemetry_modes_should_be(self, mode):
        modes = sorted({f["mode"] for f in self.get_telemetry_frames()})
        if modes != [int(mode)]:
            raise AssertionError("telemetry modes seen: %s (expected only %s)" % (modes, mode))

    def telemetry_mode_should_become_one_of(self, *modes):
        wanted = {int(m) for m in modes}
        seen = [f["mode"] for f in self.get_telemetry_frames()]
        if not any(m in wanted for m in seen):
            raise AssertionError("mode never in %s; modes seen: %s" % (sorted(wanted), sorted(set(seen))))

    def telemetry_time_should_be_monotonic(self):
        t = [f["t_ms"] for f in self.get_telemetry_frames()]
        bad = [(a, b) for a, b in zip(t, t[1:]) if b < a]
        if bad:
            raise AssertionError("t_ms went backwards (reset?): %s" % bad[:5])

    def describe_last_frames(self, n=3):
        out = []
        for f in self.frames[-int(n):]:
            out.append(format_frame(f))
        return "\n".join(out)


def format_frame(f):
    if f["type"] == 1:
        return ("TLM seq=%(seq)d t=%(t_ms)dms a=(%(ax)d,%(ay)d,%(az)d)mg g=(%(gx)d,%(gy)d,%(gz)d)x0.1dps "
                "T=%(temp)dx0.01C p=%(pressure)dPa alt=%(alt_raw)d/%(alt_filt)dcm mode=%(mode)d faults=0x%(faults)02x" % f)
    if f["type"] == 2:
        return ("STAT seq=%(seq)d up=%(uptime_ms)dms cpu=%(cpu_load)d%%o sensor=%(sensor_max_us)dus "
                "packer=%(packer_max_us)dus tx=%(tx_max_us)dus drops=%(queue_drops)d i2c_err=%(i2c_errors)d "
                "reset=%(reset_cause)d mode=%(mode)d" % f)
    return str(f)


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    frames, bad, rest = parse_frames(data)
    every = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    for k, f in enumerate(frames):
        if f["type"] == 2 or k % every == 0:
            print(format_frame(f))
    print("frames=%d bad_crc=%d leftover=%d" % (len(frames), bad, len(rest)))
