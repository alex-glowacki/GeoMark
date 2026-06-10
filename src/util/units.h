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
 * ------------------------------------------------------------------------- */

/** Extract integer degrees from decimal degrees */
static inline int gm_deg_to_dms_d(double deg) {
    return (int)deg;
}

/** Extract integer minutes from decimal degrees */
static inline int gm_deg_to_dms_m(double deg) {
    double frac = deg - (int)deg;
    return (int)(frac * 60.0);
}

/** Extract integer seconds from decimal degrees */
static inline double gm_deg_to_dms_s(double deg) {
    double frac_deg = deg - (int)deg;
    double frac_min = frac_deg * 60.0 - (int)(frac_deg * 60.0);
    return frac_min * 60.0;
}

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_UNITS_H */