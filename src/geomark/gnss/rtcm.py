"""RTCM3 message handler - reads and forwards correction data between base and rover."""

from __future__ import annotations

# RTCM3 framing constants
_RTCM3_PREAMBLE = 0xD3
_RTCM3_HEADER_LEN = 3
_RTCM3_CRC_LEN = 3


def extract_message_type(frame: bytes) -> int | None:
    """Extract the RTCM3 messgae type number from a raw frame.
    
    Args:
        frame: Raw bytes of a complete RTCM3 frame.
        
    Returns:
        Integer message type (e.g. 1005, 1074), or None if the frame is malformed.
    """
    if len(frame) < _RTCM3_HEADER_LEN + 2:
        return None
    if frame[0] != _RTCM3_PREAMBLE:
        return None
    # Message type is the top 12 bits of the payload
    msg_type = (frame[3] << 4) | (frame[4] >> 4)
    return msg_type


def is_valid_frame(frame: bytes) -> bool:
    """Return True if the frame starts with the RTCM3 preamble byte.
    
    Full CRC-24Q validation will be added in a later iteration.
    
    Args:
        frame: Raw bytes to check.
    """
    return len(frame) > 0 and frame[0] == _RTCM3_PREAMBLE