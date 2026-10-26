"""Byte sources: serial port, TCP client (Renode UART socket terminal) and file replay.

Every source implements :class:`ByteSource`: ``read()`` returns whatever bytes are
available (possibly ``b""`` after a short wait), ``eof`` becomes True when the
source is exhausted (TCP peer closed, replay file finished).
"""

from __future__ import annotations

import socket
import time
from pathlib import Path
from typing import BinaryIO, Protocol

import serial


class ByteSource(Protocol):
    @property
    def name(self) -> str: ...

    @property
    def eof(self) -> bool: ...

    def read(self) -> bytes: ...

    def close(self) -> None: ...


class SerialSource:
    """Serial port via pyserial (e.g. Nucleo ST-LINK VCP at 115200 8N1)."""

    def __init__(self, port: str, baudrate: int = 115200, timeout_s: float = 0.05) -> None:
        self._port = port
        self._ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout_s)

    @property
    def name(self) -> str:
        return f"seri:{self._port}"

    @property
    def eof(self) -> bool:
        return False

    def read(self) -> bytes:
        waiting = self._ser.in_waiting
        return bytes(self._ser.read(waiting if waiting > 0 else 1))

    def close(self) -> None:
        self._ser.close()


class TcpSource:
    """TCP client, e.g. Renode ``emulation CreateServerSocketTerminal 3456 ...``.

    Connection attempts are retried until ``connect_timeout_s`` expires so the
    ground station can be started before the emulator.
    """

    def __init__(
        self,
        host: str,
        port: int,
        connect_timeout_s: float = 10.0,
        read_timeout_s: float = 0.05,
    ) -> None:
        self._host = host
        self._port = port
        self._eof = False
        deadline = time.monotonic() + connect_timeout_s
        last_error: OSError | None = None
        while True:
            try:
                self._sock = socket.create_connection((host, port), timeout=2.0)
                break
            except OSError as exc:
                last_error = exc
                if time.monotonic() >= deadline:
                    raise ConnectionError(
                        f"{host}:{port} adresine bağlanılamadı: {last_error}"
                    ) from exc
                time.sleep(0.5)
        self._sock.settimeout(read_timeout_s)

    @property
    def name(self) -> str:
        return f"tcp:{self._host}:{self._port}"

    @property
    def eof(self) -> bool:
        return self._eof

    def read(self) -> bytes:
        if self._eof:
            return b""
        try:
            data = self._sock.recv(4096)
        except (socket.timeout, BlockingIOError):
            return b""
        except OSError:
            self._eof = True
            return b""
        if not data:
            self._eof = True
        return data

    def close(self) -> None:
        self._sock.close()


class ReplaySource:
    """Replays a raw binary capture file.

    ``rate_bps`` limits the replay speed in bytes per second (None = as fast as
    possible). 11520 B/s corresponds to a 115200 baud 8N1 line.
    """

    def __init__(self, path: str | Path, rate_bps: float | None = None, chunk: int = 1024) -> None:
        self._path = Path(path)
        self._file: BinaryIO = open(self._path, "rb")
        self._rate = rate_bps if rate_bps and rate_bps > 0 else None
        self._chunk = chunk
        self._sent = 0
        self._start = time.monotonic()
        self._eof = False

    @property
    def name(self) -> str:
        return f"dosya:{self._path.name}"

    @property
    def eof(self) -> bool:
        return self._eof

    def read(self) -> bytes:
        if self._eof:
            return b""
        size = self._chunk
        if self._rate is not None:
            allowed = int((time.monotonic() - self._start) * self._rate) - self._sent
            if allowed <= 0:
                time.sleep(0.01)
                return b""
            size = min(size, allowed)
        data = self._file.read(size)
        if not data:
            self._eof = True
            return b""
        self._sent += len(data)
        return data

    def close(self) -> None:
        self._file.close()


def parse_host_port(text: str, default_host: str = "localhost") -> tuple[str, int]:
    """Parse ``host:port`` or ``port``."""
    if ":" in text:
        host, _, port = text.rpartition(":")
        return (host or default_host, int(port))
    return (default_host, int(text))
