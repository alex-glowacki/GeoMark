/**
 * @file rtcm3.c
 * @brief RTCM3 frame synchronization and decoding implementation.
 */

#include "rtcm3.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * CRC-24Q
 * ---------------------------------------------------------------------- */

/*
 * CRC-24Q polynomial: 0x1864CFB
 * Reference: RTKLIB src/rtkcmn.c, crc24q()
 * Standard: RTCM10403.3 §5.1, also used in PGP/OpenPGP (RFC 4880).
 *
 * Bit-by-bit (no lookup table) — suitable for the Pi Zero W's limited RAM
 * and avoids a 1 KB static table. Throughput is not a bottleneck here:
 * the largest RTCM3 frame is 1029 bytes, processed at ~115200 baud.
 */
#define CRC24Q_POLY 0x1864CFBu

uint32_t rtcm3_crc24(const uint8_t *buf, size_t len) {
    uint32_t crc = 0;

    while (len--) {
        crc ^= ((uint32_t)(*buf++) << 16);
        for (int i = 0; i < 8; i++) {
            crc <<= 1;
            if (crc & 0x1000000u) {
                crc ^= CRC24Q_POLY;
            }
        }
    }

    return crc & 0xFFFFFFu;
}

/* -------------------------------------------------------------------------
 * Frame synchronization
 * ---------------------------------------------------------------------- */

/*
 * RTCM3 frame layout (RTCM10403.3 §4.1):
 *
 *   Byte 0:      0xD3              — preamble
 *   Byte 1:      000000xx          — 6 reserved bits (must be 0), bits[9:8] of length
 *   Byte 2:      xxxxxxxx          — bits[7:0] of length
 *   Bytes 3..N:  payload           — N = length bytes
 *   Bytes N+1..N+3: CRC-24Q       — over bytes 0..N (header + payload)
 *
 * Total frame size = 3 (header) + length (payload) + 3 (CRC) = length + 6.
 */

int rtcm3_find_frame(const uint8_t *buf, size_t buf_len, size_t *frame_start, size_t *payload_len) {
    if (!buf || !frame_start || !payload_len) {
        return 0;
    }

    for (size_t i = 0; i < buf_len; i++) {

        /* Step 1: locate preamble byte */
        if (buf[i] != RTCM3_PREAMBLE) {
            continue;
        }

        /* Step 2: need at least 6 bytes from here to form a minimal frame */
        if (buf_len - i < RTCM3_MIN_FRAME_LEN) {
            break;  /* not enough data even for minimum frame */
        }

        /* Step 3: check that the 6 reserved bits in header byte 1 are zero */
        if ((buf[i + 1] & 0xFCu) != 0) {
            continue; /* reserved bits set — not a valid RTCM3 header */
        }

        /* Step 4: extract 10-bit payload length */
        size_t plen = (((size_t)(buf[i + 1] & 0x03u)) << 8) | (size_t)buf[i + 2];

        /* Step 5: check that the full frame fits in the buffer */
        size_t frame_len = plen + RTCM3_MIN_FRAME_LEN;
        if (buf_len - i < frame_len) {
            break;  /* frame extends beyond available data */
        }

        /* Step 6: validate CRC-24Q over header (3 bytes) + payload (plen bytes) */
        uint32_t computed = rtcm3_crc24(&buf[i], 3 + plen);
        uint32_t stored = ((uint32_t)buf[i + 3 + plen] << 16)
                        | ((uint32_t)buf[i + 3 + plen + 1] << 8)
                        | ((uint32_t)buf[i + 3 + plen + 2]);

        if (computed != stored) {
            continue; /* CRC mismatch — spurious 0xD3 byte, keep scanning */
        }

        /* Valid frame found */
        *frame_start = i;
        *payload_len = plen;
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Message type extraction
 * ---------------------------------------------------------------------- */

/*
 * The first 12 bits of the payload encode the message type (big-endian):
 *
 *   payload[0]:  bits[11:4] of message type
 *   payload[1]:  bits[3:0]  of message type in the high nibble
 *
 *   type = (payload[0] << 4) | (payload[1] >> 4)
 */

int rtcm3_decode(const uint8_t *payload, size_t payload_len) {
    if (!payload || payload_len < 2) {
        return -1;
    }

    return (int)(((uint32_t)payload[0] << 4) | ((uint32_t)payload[1] >> 4));
}