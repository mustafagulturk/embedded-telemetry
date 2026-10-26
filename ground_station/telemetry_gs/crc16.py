"""CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000).

Check value: crc16_ccitt_false(b"123456789") == 0x29B1
"""

from __future__ import annotations

POLY = 0x1021
INIT = 0xFFFF


def _build_table() -> tuple[int, ...]:
    table: list[int] = []
    for byte in range(256):
        crc = byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ POLY) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
        table.append(crc)
    return tuple(table)


_TABLE: tuple[int, ...] = _build_table()


def crc16_ccitt_false(data: bytes | bytearray | memoryview, crc: int = INIT) -> int:
    """Compute CRC-16/CCITT-FALSE over ``data``.

    ``crc`` may be passed to continue a running computation over several chunks.
    """
    table = _TABLE
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ table[((crc >> 8) ^ b) & 0xFF]
    return crc
