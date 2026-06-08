/**
 * @file rtcm3.h
 * @brief RTCM3 binary frame sync and length decoder.
 */

#ifndef GEOMARK_RTCM3_H
#define GEOMARK_RTCM3_H

#include <stddef.h>
#include <stdint.h>

#include "geomark.h"

#define RTCM3_PREAMBLE 0xD3U

typedef struct {
    uint16_t message_type;
    uint16_t payload_len;
    uint8_t payload[GM_RTCM3_MAX_LEN];
    uint32_t crc24;
} gm_rtcm3_frame_t;

gm_status_t rtcm3_find_frame(const uint8_t *buf, size_t len, size_t *frame_start,
                             size_t *frame_len);
gm_status_t rtcm3_decode(const uint8_t *buf, size_t len, gm_rtcm3_frame_t *out);
uint32_t rtcm3_crc24(const uint8_t *buf, size_t len);

#endif /* GEOMARK_RTCM3_H */