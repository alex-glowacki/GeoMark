/**
 * @file test_rtcm3.c
 * @brief Unit tests for RTCM3 CRC-24Q, frame sync, and message type decoding.
 *
 * All test vectors pre-verified with Python before embedding:
 *
 *   POLY = 0x1864CFB
 *   def crc24q(data):
 *       crc = 0
 *       for b in data:
 *           crc ^= (b << 16)
 *           for _ in range(8):
 *               crc <<= 1
 *               if crc & 0x1000000: crc ^= POLY
 *       return crc & 0xFFFFFF
 *
 *   frame    = bytes.fromhex("D300063ED000000000")
 *   crc      = crc24q(frame)   →  0xF3517B
 *   msg_type = (0x3E << 4) | (0xD0 >> 4)  →  1005
 */

#include "../src/gnss/rtcm3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Minimal test harness (mirrors test_nmea.c style)
 * ---------------------------------------------------------------------- */

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT(cond, name)                                          \
    do {                                                            \
        if (cond) {                                                 \
            printf("  PASS: %s\n", (name));                         \
            g_passed++;                                             \
        } else {                                                    \
            printf("  FAIL: %s  (line %d)\n", (name), __LINE__);    \
            g_failed++;                                             \
        }                                                           \
    } while (0)

/* -------------------------------------------------------------------------
 * Test vectors (Python-verified)
 * ---------------------------------------------------------------------- */

/*
 * Minimal synthetic RTCM3 frame:
 *   Header:  D3 00 06          (preamble, reserved=0, length=6)
 *   Payload: 3E D0 00 00 00 00 (message type 1005 encoded in first 12 bits)
 *   CRC:     F3 51 7B          (CRC-24Q over the 9 header+payload bytes)
 */
static const uint8_t GOOD_FRAME[] = {
    0xD3, 0x00, 0x06,                   /* header */
    0x3E, 0xD0, 0x00, 0x00, 0x00, 0x00, /* payload (6 bytes) */
    0xF3, 0x51, 0x7B                    /* CRC-24Q */
};
static const size_t GOOD_FRAME_LEN      = sizeof(GOOD_FRAME);   /* 12 */
static const size_t GOOD_PAYLOAD_LEN    = 6;
static const int    GOOD_MSG_TYPE       = 1005;
static const uint32_t GOOD_CRC          = 0xF3517Bu;

/* -------------------------------------------------------------------------
 * Test: CRC-24Q correctness
 * ---------------------------------------------------------------------- */

static void test_crc24q(void) {
    printf("--- CRC-24Q ---\n");

    /* Empty input → 0 */
    ASSERT(rtcm3_crc24(NULL, 0) == 0 || rtcm3_crc24((const uint8_t *)"", 0) == 0,
           "crc24q empty input is 0");
    
    /* Known header+payload → expected CRC */
    uint32_t crc = rtcm3_crc24(GOOD_FRAME, 9);  /* header(3) + payload(6) */
    ASSERT(crc == GOOD_CRC, "crc24q known vector matches 0xF3517B");

    /* Single-byte flip changes CRC */
    uint8_t corrupted[9];
    for (size_t i = 0; i < 9; i++) corrupted[i] = GOOD_FRAME[i];
    corrupted[4] ^= 0xFF;
    ASSERT(rtcm3_crc24(corrupted, 9) != GOOD_CRC,
           "crc24q detects single corrupted byte");
}

/* -------------------------------------------------------------------------
 * Test: rtcm3_find_frame()
 * ---------------------------------------------------------------------- */

static void test_find_frame(void) {
    printf("--- rtcm3_find_frame ---\n");

    size_t start = 0, plen = 0;
    int found;

    /* Good frame at offset 0 */
    found = rtcm3_find_frame(GOOD_FRAME, GOOD_FRAME_LEN, &start, &plen);
    ASSERT(found == 1,              "find_frame: good frame found");
    ASSERT(start == 0,              "find_frame: good frame at offset 0");
    ASSERT(plen == GOOD_PAYLOAD_LEN, "find_frame: payload length = 6");

    /* Good frame preceded by 3 garbage bytes → found at offset 3 */
    uint8_t prefixed[3 + GOOD_FRAME_LEN];
    prefixed[0] = 0x00; prefixed[1] = 0xAB; prefixed[2] = 0xFF;
    for (size_t i = 0; i < GOOD_FRAME_LEN; i++) prefixed[3 + i] = GOOD_FRAME[i];

    start = 0; plen = 0;
    found = rtcm3_find_frame(prefixed, sizeof(prefixed), &start, &plen);
    ASSERT(found == 1, "find_frame: frame found after garbage prefix");
    ASSERT(start == 3, "find_frame: frame start at offset = 3");

    /* Buffer too short to contain a complete frame → not found */
    found = rtcm3_find_frame(GOOD_FRAME, RTCM3_MIN_FRAME_LEN - 1, &start, &plen);
    ASSERT(found == 0, "find_frame: truncated buffer returns 0");

    /* Corrupt last CRC byte → CRC mismatch → not found */
    uint8_t bad_frame[GOOD_FRAME_LEN];
    for (size_t i = 0; i < GOOD_FRAME_LEN; i++) bad_frame[i] = GOOD_FRAME[i];
    bad_frame[GOOD_FRAME_LEN - 1] ^= 0xFF;
    found = rtcm3_find_frame(bad_frame, GOOD_FRAME_LEN, &start, &plen);
    ASSERT(found == 0, "find_frame: bad CRC rejected");

    /* NULL buffer → not found (no crash) */
    found = rtcm3_find_frame(NULL, 0, &start, &plen);
    ASSERT(found == 0, "find_frame: NULL buffer returns 0");
}

/* -------------------------------------------------------------------------
 * Test: rtcm3_decode()
 * ---------------------------------------------------------------------- */

static void test_decode(void) {
    printf("--- rtcm3_decode ---\n");

    const uint8_t *payload = &GOOD_FRAME[3]; /* payload starts at byte 3 */

    /* Known payload → message type 1005 */
    int msg_type = rtcm3_decode(payload, GOOD_PAYLOAD_LEN);
    ASSERT(msg_type == GOOD_MSG_TYPE, "decode: message type = 1005");

    /* payload_len < 2 → -1 */
    ASSERT(rtcm3_decode(payload, 1) == -1, "decode: payload_len=1 return -1");
    ASSERT(rtcm3_decode(payload, 0) == -1, "decode: payload_len=0 return -1");

    /* NULL pointer → -1 (no crash) */
    ASSERT(rtcm3_decode(NULL, 6) == -1, "decode: NULL payload return -1");
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void) {
    printf("=== test_rtcm3 ===\n");
    test_crc24q();
    test_find_frame();
    test_decode();
    printf("==================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}