/**
 * @file geomark.h
 * @brief Shared types, constants, and version information for GeoMark.
 */

#ifndef GEOMARK_H
#define GEOMARK_H

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Version
 * ---------------------------------------------------------------------- */

#define GEOMARK_VERSION_MAJOR 0
#define GEOMARK_VERSION_MINOR 1
#define GEOMARK_VERSION_PATCH 0
#define GEOMARK_VERSION_STRING "0.1.0"

/* -------------------------------------------------------------------------
 * Runtime mode
 * ---------------------------------------------------------------------- */

typedef enum {
    GEOMARK_MODE_UNKNOWN = 0,
    GEOMARK_MODE_BASE,
    GEOMARK_MODE_ROVER,
    GEOMARK_MODE_UI,         /* Handheld Pi 5: receives packets, drives TFT */
    GEOMARK_MODE_STATIC_LOG, /* Raw observation logging for OPUS --
                              * see staticlog/station.h */
} geomark_mode_t;

/* -------------------------------------------------------------------------
 * Return codes
 * ---------------------------------------------------------------------- */

typedef enum {
    GM_OK = 0,
    GM_ERR_GENERIC = -1,
    GM_ERR_IO = -2,
    GM_ERR_TIMEOUT = -3,
    GM_ERR_PARSE = -4,
    GM_ERR_CONFIG = -5,
    GM_ERR_NOMEM = -6,
} gm_status_t;

/* -------------------------------------------------------------------------
 * GNSS fix types
 * ---------------------------------------------------------------------- */

typedef enum {
    FIX_NONE = 0,
    FIX_SINGLE = 1,
    FIX_DGPS = 2,
    FIX_RTK_FLOAT = 5,
    FIX_RTK_FIXED = 4,
} gm_fix_type_t;

/* -------------------------------------------------------------------------
 * GNSS position
 * ---------------------------------------------------------------------- */

typedef struct {
    double latitude;  /* degrees, WGS84 */
    double longitude; /* degrees, WGS84 */
    double altitude;  /* meters above ellipsoid */
    float hdop;
    uint8_t satellites;
    gm_fix_type_t fix_type;
    uint32_t timestamp_ms; /* system uptime at fix time */
} gm_position_t;

/* -------------------------------------------------------------------------
 * Buffer sizes
 * ---------------------------------------------------------------------- */

#define GM_SERIAL_BUF_SIZE 4096
#define GM_NMEA_MAX_LEN 256
#define GM_RTCM3_MAX_LEN 4096
#define GM_CONFIG_MAX_LINE 256
#define GM_LOG_MAX_LINE 512

#endif /* GEOMARK_H */