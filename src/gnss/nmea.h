/**
 * @file nmea.h
 * @brief NMEA 0183 sentence parser.
 */

#ifndef GEOMARK_NMEA_H
#define GEOMARK_NMEA_H

#include <stdbool.h>
#include <stdint.h>

#include "geomark.h"

/** Parsed GGA sentence fields */
typedef struct {
    int hour;
    int minute;
    int second;
    double latitude;  /* degrees, positive = North */
    double longitude; /* degrees, positive = East  */
    int fix_quality;
    int satellites;
    float hdop;
    double altitude; /* metres MSL */
} gm_nmea_gga_t;

/** Parsed RMC sentence fields */
typedef struct {
    int hour;
    int minute;
    int second;
    bool valid;
    double latitude;
    double longitude;
    float speed_knots;
    float course_deg;
} gm_nmea_rmc_t;

bool nmea_checksum_valid(const char *sentence);
gm_status_t nmea_parse_gga(const char *sentence, gm_nmea_gga_t *out);
gm_status_t nmea_parse_rmc(const char *sentence, gm_nmea_rmc_t *out);

#endif /* GEOMARK_NMEA_H */