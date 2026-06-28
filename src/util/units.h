/* src/util/units.h
 *
 * Unit conversion — GeoMark stores all internal values in SI / WGS-84:
 *   positions  : decimal degrees (WGS-84 latitude / longitude)
 *   distances  : metres
 *   altitudes  : metres above mean sea level (MSL)
 *   speed      : knots (native NMEA), metres/sec elsewhere
 *
 * Display and export use US customary / US Survey units.
 * All conversions are exact or use the internationally accepted constants.
 *
 * Reference:
 *   NIST SP 811 (2008 ed.) — Guide for the Use of the International System
 *   of Units (SI), Appendix B.8 and B.9
 *   US Survey Foot defined as 1200/3937 m (exact) per 30 FR 9217 (1959),
 *   retained for GIS/surveying applications per NOAA/NGS guidance.
 */
#ifndef GEOMARK_UNITS_H
#define GEOMARK_UNITS_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Conversion constants
 * ------------------------------------------------------------------------- */

/** International foot: exactly 0.3048 m (NIST SP 811) */
#define GM_M_PER_INTL_FOOT 0.3048

/**
 * US Survey foot: exactly 1200/3937 m (~0.30480060960 m).
 * Used for all horizontal survey measurements to match NGS/USGS datum output.
 * NOTE: The US Survey foot is being deprecated by NIST effective 2023 for
 * most purposes, but NGS continues to use it for legacy datum consistency.
 * GeoMark uses it for exported point coordinates to match existing US survey
 * control monuments. Vertical (elevation) values use the international foot.
 */
#define GM_M_PER_SURVEY_FOOT (1200.0 / 3937.0)

/** Knots to meters per second (exact via NM definition) */
#define GM_MPS_PER_KNOT 0.514444444444

/* -------------------------------------------------------------------------
 * Distance / elevation
 * ------------------------------------------------------------------------- */

/** Meters → US Survey feet (horizontal - coordinates, offsets) */
static inline double gm_m_to_survey_ft(double m) {
    return m / GM_M_PER_SURVEY_FOOT;
}

/** US Survey feet → meters */
static inline double gm_survey_ft_to_m(double ft) {
    return ft * GM_M_PER_SURVEY_FOOT;
}

/** Meters → international feet (vertical - elevations, heights) */
static inline double gm_m_to_intl_ft(double m) {
    return m / GM_M_PER_INTL_FOOT;
}

/** International feet → meters */
static inline double gm_intl_ft_to_m(double ft) {
    return ft * GM_M_PER_INTL_FOOT;
}

/* -------------------------------------------------------------------------
 * Speed
 * ------------------------------------------------------------------------- */

/** Knots → meters per second */
static inline double gm_knots_to_mps(double knots) {
    return knots * GM_MPS_PER_KNOT;
}

/** Knots → miles per hour (1 knot = 1.15077945 mph exactly) */
static inline double gm_knots_to_mph(double knots) {
    return knots * 1.15077945;
}

/** Meters per second - knots */
static inline double gm_mps_to_knots(double mps) {
    return mps / GM_MPS_PER_KNOT;
}

/* -------------------------------------------------------------------------
 * Coordinate formatting helpers
 * Conversions between decimal degrees and DMS — useful for TFT display
 * and exported report headers.
 *
 * gm_deg_to_dms_d()/_m()/_s() below operate on the MAGNITUDE of the input
 * (fabs(deg)) and always return non-negative values, regardless of
 * whether deg itself is positive or negative. This is the only sane
 * convention for DMS display -- "-97 deg 1 min 58.44 sec" (negative
 * degrees with negative minutes/seconds layered on top) is not a
 * standard or readable way to write a coordinate; the correct
 * convention is a single sign (or hemisphere letter) on the whole
 * coordinate, with degrees/minutes/seconds all reported as positive
 * magnitudes, e.g. "97 deg 1 min 58.44 sec W". Use gm_lat_bearing()/
 * gm_lon_bearing() below (or gm_format_dms_bearing()) to get the
 * hemisphere letter that the sign of the original decimal-degrees value
 * implies, rather than reading it back out of these magnitude-only
 * functions.
 * ------------------------------------------------------------------------- */

/** Extract integer degrees (magnitude) from decimal degrees. Always >= 0
 *  -- see this section's file-level doc comment for why sign is handled
 *  separately via gm_lat_bearing()/gm_lon_bearing(), not folded into a
 *  negative degrees/minutes/seconds triple. */
static inline int gm_deg_to_dms_d(double deg) {
    return (int)fabs(deg);
}

/** Extract integer minutes (magnitude) from decimal degrees. Always >= 0
 *  -- see gm_deg_to_dms_d()'s doc comment. */
static inline int gm_deg_to_dms_m(double deg) {
    double mag = fabs(deg);
    double frac = mag - (int)mag;
    return (int)(frac * 60.0);
}

/** Extract seconds (magnitude) from decimal degrees. Always >= 0 -- see
 *  gm_deg_to_dms_d()'s doc comment. */
static inline double gm_deg_to_dms_s(double deg) {
    double mag = fabs(deg);
    double frac_deg = mag - (int)mag;
    double frac_min = frac_deg * 60.0 - (int)(frac_deg * 60.0);
    return frac_min * 60.0;
}

/** Hemisphere letter for a latitude in decimal degrees, WGS-84 convention
 *  (positive = North, matching every lat field already in this codebase,
 *  e.g. RtkFeedPosition::lat, MeasurePoint::lat). 0.0 reports 'N' --
 *  the equator has no negative side to disambiguate from, so this is an
 *  arbitrary but harmless choice, never actually exercised by a real
 *  GNSS fix. */
static inline char gm_lat_bearing(double lat) {
    return (lat < 0.0) ? 'S' : 'N';
}

/** Hemisphere letter for a longitude in decimal degrees, WGS-84
 *  convention (positive = East, matching every lon field already in
 *  this codebase). 0.0 reports 'E' -- same arbitrary-but-harmless
 *  convention as gm_lat_bearing()'s 0.0 case, for the same reason. */
static inline char gm_lon_bearing(double lon) {
    return (lon < 0.0) ? 'W' : 'E';
}

/**
 * Format a single coordinate (latitude OR longitude, in decimal degrees)
 * as a DMS string with a trailing hemisphere letter, e.g.
 * "47d55'31.08\"N" or "97d1'58.44\"W" -- the on-screen convention Measure
 * Points' live-fix readout uses (see measure_points_screen_draw.c).
 * Degrees use a literal 'd' rather than the Unicode degree sign (U+00B0)
 * -- ui/tft/display.c's built-in bitmap font only covers printable ASCII
 * (0x20-0x7E, see that file's s_font5x7 table), so a multi-byte UTF-8
 * degree sign would render as garbage/blank glyphs on the real panel,
 * not an actual degree mark. 'd' is the same ASCII-safe convention many
 * GPS receivers and plain-text NMEA-adjacent tools already use for this
 * exact reason.
 *
 * is_latitude selects which bearing function decides the trailing
 * letter (gm_lat_bearing() vs gm_lon_bearing()) -- this function has no
 * way to infer that from the numeric value alone (e.g. 47.0 is a
 * perfectly valid longitude too), so the caller must say which.
 *
 * Returns the number of characters written (excluding the NUL
 * terminator), or a negative value on truncation/encoding error, same
 * convention snprintf() itself uses -- callers that don't care can
 * ignore the return value, same as every other snprintf()-based
 * formatter in this codebase (e.g. measure_points_export.c's CSV row
 * writer).
 */
static inline int gm_format_dms_bearing(double deg, bool is_latitude, char *out, size_t out_cap) {
    int d = gm_deg_to_dms_d(deg);
    int m = gm_deg_to_dms_m(deg);
    double s = gm_deg_to_dms_s(deg);
    char bearing = is_latitude ? gm_lat_bearing(deg) : gm_lon_bearing(deg);
    return snprintf(out, out_cap, "%dd%d'%.2f\"%c", d, m, s, bearing);
}

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_UNITS_H */