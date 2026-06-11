/**
 * @file rtcm3.h
 * @brief RTCM3 frame synchronization and decoding interface.
 *
 * Covers:
 *   - CRC-24Q integrity check (RTCM10403.3 §5.1)
 *   - Frame sync: preamble (0xD3) + 10-bit length + CRC validation
 *   - Message type extraction from first 12 payload bits
 *
 * Internal data flow:
 *   raw bytes → rtcm3_find_frame() → rtcm3_decode() → message type
 */

#ifndef GEOMARK_RTCM3_H
#define GEOMARK_RTCM3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

/** Sync byte that begins every RTCM3 frame. */
#define RTCM3_PREAMBLE 0xD3u

/** Minimum frame size: 3-byte header + 0-byte payload + 3-byte CRC. */
#define RTCM3_MIN_FRAME_LEN 6u

/** Maximum payload length encoded in the 10-bit length field. */
#define RTCM3_MAX_PAYLOAD 1023u

/** Total maximum frame size: header(3) + max_payload(1023) + CRC(3). */
#define RTCM3_MAX_FRAME_LEN (RTCM3_MIN_FRAME_LEN + RTCM3_MAX_PAYLOAD)

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Compute CRC-24Q over a byte buffer.
 *
 * Algorithm: CRC-24Q, polynomial 0x1864CFB, initial value 0.
 * Identical to the implementation in RTKLIB src/rtkcmn.c.
 *
 * @param buf   Pointer to input bytes.
 * @param len   Number of bytes to process.
 * @return      24-bit CRC (upper 8 bits of return value are always 0).
 */
uint32_t rtcm3_crc24(const uint8_t *buf, size_t len);

/**
 * @brief Scan a byte buffer for a valid RTCM3 frame.
 *
 * Searches forward from buf[0] for a 0xD3 preamble, then validates:
 *   1. The 6-reserved-bits in header byte 1 are zero.
 *   2. The 10-bit length field fits within the remaining buffer.
 *   3. CRC-24Q over header+payload matches the trailing 3 CRC bytes.
 *
 * On success, *frame_start is set to the index of the 0xD3 byte and
 * *payload_len is set to the number of payload bytes.
 *
 * @param buf           Input buffer.
 * @param buf_len       Number of valid bytes in buf.
 * @param frame_start   [out] Index into buf where the frame begins.
 * @param payload_len   [out] Number of payload bytes (0–1023).
 * @return              1 if a valid frame was found, 0 otherwise.
 */
int rtcm3_find_frame(const uint8_t *buf, size_t buf_len, size_t *frame_start, size_t *payload_len);

/**
 * @brief Extract the 12-bit message type from a payload buffer.
 *
 * The message type occupies bits 0–11 of the payload (big-endian):
 *   type = (payload[0] << 4) | (payload[1] >> 4)
 *
 * The caller must ensure payload_len >= 2 before calling.
 *
 * @param payload       Pointer to first byte of the payload.
 * @param payload_len   Length of the payload in bytes.
 * @return              Message type (0–4095), or -1 if payload_len < 2.
 */
int rtcm3_decode(const uint8_t *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_RTCM3_H */