/* src/gnss/nmea.h */
#ifndef GEOMARK_NMEA_H
#define GEOMARK_NMEA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Parsed GGA sentence — essential fix data
 * Internal storage: decimal degrees (WGS-84), metres
 * ------------------------------------------------------------------------- */
typedef struct {
    double lat;          /* decimal degrees, positive = North */
    double lon;          /* decimal degrees, positive = East */
    double alt_msl;      /* altitude above mean sea level, meters */
    double hdop;         /* horizontal dilution of precision */
    uint8_t fix_quality; /* 0=invalid 1=GPS 2=DGPS 4=RTK 5=Float RTK */
    uint8_t num_sats;    /* satellites used in fix */
    bool valid;          /* true if parse succeeded */
} NmeaGga;

/* -------------------------------------------------------------------------
 * Parsed RMC sentence — recommended minimum navigation data
 * Internal storage: decimal degrees (WGS-84), knots
 * ------------------------------------------------------------------------- */
typedef struct {
    double lat;         /* decimal degrees, positive = North */
    double lon;         /* decimal degrees, positive = East */
    double speed_knots; /* speed over ground, knots */
    double course_deg;  /* course over ground, degrees true */
    bool active;        /* true = data valid ('A'), false = void ('V') */
    bool valid;         /* true if parse succeeded */
} NmeaRmc;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Validate the NMEA checksum of a complete sentence.
 * @param sentence  Null-terminated string beginning with '$'.
 * @return          true if checksum is present and correct.
 */
bool nmea_checksum_valid(const char *sentence);

/**
 * Parse a GGA sentence into @p out.
 * @param sentence  Null-terminated GGA string beginning with '$'.
 * @param out       Destination struct; out->valid set to false on failure.
 * @return          true on success.
 */
bool nmea_parse_gga(const char *sentence, NmeaGga *out);

/**
 * Parse an RMC sentence into @p out.
 * @param sentence  Null-terminated RMC string beginning with '$'.
 * @param out       Destination struct; out->valid set to false on failure.
 * @return          true on success.
 */
bool nmea_parse_rmc(const char *sentence, NmeaRmc *out);

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_NMEA_H */