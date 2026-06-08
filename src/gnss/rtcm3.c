/**
 * @file rtcm3.c
 * @brief RTCM3 binary frame implementation.
 */

#include "gnss/rtcm3.h"
#include "util/log.h"

gm_status_t rtcm3_find_frame(const uint8_t *buf, size_t len,
                              size_t *frame_start, size_t *frame_len)
{
    /* TODO: Phase 1 implementation */
    (void)buf;
    (void)len;
    (void)frame_start;
    (void)frame_len;
    return GM_ERR_PARSE;
}

gm_status_t rtcm3_decode(const uint8_t *buf, size_t len, gm_rtcm3_frame_t *out)
{
    /* TODO: Phase 1 implementation */
    (void)buf;
    (void)len;
    (void)out;
    return GM_ERR_PARSE;
}

uint32_t rtcm3_crc24(const uint8_t *buf, size_t len)
{
    /* TODO: Phase 1 implementation */
    (void)buf;
    (void)len;
    return 0;
}