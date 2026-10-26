"""Ground station for the embedded telemetry board (packet format v1)."""

from .crc16 import crc16_ccitt_false
from .protocol import (
    Frame,
    FrameDecoder,
    Mode,
    SeqTracker,
    Stats,
    Telemetry,
    encode_frame,
    encode_message,
)

__all__ = [
    "Frame",
    "FrameDecoder",
    "Mode",
    "SeqTracker",
    "Stats",
    "Telemetry",
    "crc16_ccitt_false",
    "encode_frame",
    "encode_message",
]

__version__ = "0.1.0"
