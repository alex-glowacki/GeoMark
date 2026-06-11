/* src/gnss/nmea.c */
#include "gnss/nmea.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * Extract the Nth comma-delimited field (0-indexed) from a NMEA sentence.
 * Stops at '*' (checksum delimiter) or end of string.
 * Returns true if the field index exists (value may be empty string).
 */
static bool nmea_field(const char *sentence, int n, char *buf, size_t buf_len)
{
    const char *p = sentence;
    int idx = 0;

    while (*p && *p != '*') {
        if (idx == n) {
            size_t i = 0;
            while (*p && *p != ',' && *p != '*' && i < buf_len - 1) {
                buf[i++] = *p++;
            }
            buf[i] = '\0';
            return true;
        }
        if (*p == ',') idx++;
        p++;
    }

    buf[0] = '\0';
    return false;
}

/**
 * Return true if the sentence type field (field 0, after '$') ends with
 * the given 3-character type tag (e.g. "GGA", "RMC").
 * Handles any talker prefix: $GPGGA, $GNGGA, $GLGGA, etc.
 */
static bool nmea_is_type(const char *sentence, const char *type)
{
    if (!sentence || *sentence != '$') return false;
    /* Field 0 is the talker+type string, e.g. "GNGGA" or "GPGGA".
     * We only care that it ends with 'type' (last 3 characters). */
    const char *comma = strchr(sentence, ',');
    if (!comma) return false;
    /* Length of field 0 content (excluding '$' and ','). */
    ptrdiff_t field_len = comma - (sentence + 1);
    if (field_len < 3) return false;
    /* Compare the last 3 characters of field 0 against 'type'. */
    return strncmp(comma - 3, type, 3) == 0;
}

/**
 * Convert NMEA DDmm.mmmm / DDDmm.mmmm to decimal degrees.
 * Returns 0.0 on empty input — callers rely on NmeaGga/NmeaRmc.valid instead.
 */
static double ddmm_to_deg(const char *s)
{
    if (!s || *s == '\0') return 0.0;
    double raw = atof(s);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100.0);
    return (double)degrees + minutes / 60.0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

bool nmea_checksum_valid(const char *sentence)
{
    if (!sentence || *sentence != '$') return false;

    const char *p = sentence + 1; /* skip '$' */
    uint8_t calc = 0;

    while (*p && *p != '*') {
        calc ^= (uint8_t)*p++;
    }

    if (*p != '*') return false;
    p++; /* skip '*' */

    if (p[0] == '\0' || p[1] == '\0') return false;

    char hex[3] = { p[0], p[1], '\0' };
    uint8_t expected = (uint8_t)strtoul(hex, NULL, 16);
    return calc == expected;
}

bool nmea_parse_gga(const char *sentence, NmeaGga *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    if (!nmea_is_type(sentence, "GGA")) return false;
    if (!nmea_checksum_valid(sentence)) return false;

    char f[32];

    /* Field 2 - latitude (DDmm.mmmm) */
    if (!nmea_field(sentence, 2, f, sizeof(f)) || f[0] == '\0') return false;
    double lat = ddmm_to_deg(f);

    /* Field 3 - N/S */
    if (!nmea_field(sentence, 3, f, sizeof(f))) return false;
    if (f[0] == 'S') lat = -lat;

    /* Field 4 - longitude (DDDmm.mmmm) */
    if (!nmea_field(sentence, 4, f, sizeof(f)) || f[0] == '\0') return false;
    double lon = ddmm_to_deg(f);

    /* Field 5 - E/W */
    if (!nmea_field(sentence, 5, f, sizeof(f))) return false;
    if (f[0] == 'W') lon = -lon;

    /* Field 6 - fix quality */
    if (!nmea_field(sentence, 6, f, sizeof(f)) || f[0] == '\0') return false;
    uint8_t fix_quality = (uint8_t)atoi(f);

    /* Field 7 - number of satellites */
    if (!nmea_field(sentence, 7, f, sizeof(f)) || f[0] == '\0') return false;
    uint8_t num_sats = (uint8_t)atoi(f);

    /* Field 8 - HDOP */
    if (!nmea_field(sentence, 8, f, sizeof(f)) || f[0] == '\0') return false;
    double hdop = atof(f);

    /* Field 9 - altitude MSL, meters */
    if (!nmea_field(sentence, 9, f, sizeof(f)) || f[0] == '\0') return false;
    double alt_msl = atof(f);

    out->lat         = lat;
    out->lon         = lon;
    out->alt_msl     = alt_msl;
    out->hdop        = hdop;
    out->fix_quality = fix_quality;
    out->num_sats    = num_sats;
    out->valid       = true;
    return true;
}

bool nmea_parse_rmc(const char *sentence, NmeaRmc *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    if (!nmea_is_type(sentence, "RMC")) return false;
    if (!nmea_checksum_valid(sentence)) return false;

    char f[32];

    /* Field 2 - status: 'A' = active, 'V' = void */
    if (!nmea_field(sentence, 2, f, sizeof(f)) || f[0] == '\0') return false;
    bool active = (f[0] == 'A');

    /* Field 3 - latitude */
    if (!nmea_field(sentence, 3, f, sizeof(f)) || f[0] == '\0') return false;
    double lat = ddmm_to_deg(f);

    /* Field 4 - N/S */
    if (!nmea_field(sentence, 4, f, sizeof(f))) return false;
    if (f[0] == 'S') lat = -lat;

    /* Field 5 - longitude */
    if (!nmea_field(sentence, 5, f, sizeof(f)) || f[0] == '\0') return false;
    double lon = ddmm_to_deg(f);

    /* Field 6 - E/W */
    if (!nmea_field(sentence, 6, f, sizeof(f))) return false;
    if (f[0] == 'W') lon = -lon;

    /* Field 7 - speed over ground, knots */
    if (!nmea_field(sentence, 7, f, sizeof(f)) || f[0] == '\0') return false;
    double speed_knots = atof(f);

    /* Field 8 - course over ground, degrees true */
    if (!nmea_field(sentence, 8, f, sizeof(f)) || f[0] == '\0') return false;
    double course_deg = atof(f);

    out->lat         = lat;
    out->lon         = lon;
    out->speed_knots = speed_knots;
    out->course_deg  = course_deg;
    out->active      = active;
    out->valid       = true;
    return true;
}