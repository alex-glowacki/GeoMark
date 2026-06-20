/**
 * @file coords.h
 * @brief WGS84 and local grid coordinate transforms.
 */

#ifndef GEOMARK_COORDS_H
#define GEOMARK_COORDS_H

#include "geomark.h"

typedef struct {
    double x;
    double y;
    double z;
} gm_ecef_t;

typedef struct {
    double northing;
    double easting;
    double zone_number;
    int zone_letter;
} gm_utm_t;

/**
 * NAD83(1986) North Dakota State Plane, North Zone (EPSG:2265).
 * Output unit is the International foot (0.3048 m exactly) -- NOT the US
 * Survey foot units.h otherwise uses for survey horizontal measurements.
 * EPSG:2265 explicitly defines this zone's coordinate unit as the
 * international foot (confirmed against the zone's own EPSG WKT
 * definition: UNIT["foot",0.3048,...]), so coords_wgs84_to_nd_north()
 * converts using GM_M_PER_INTL_FOOT, not GM_M_PER_SURVEY_FOOT.
 */
typedef struct {
    double easting_ft;  /* International feet */
    double northing_ft; /* International feet */
} gm_state_plane_t;

gm_status_t coords_wgs84_to_ecef(double lat, double lon, double alt, gm_ecef_t *out);
gm_status_t coords_wgs84_to_utm(double lat, double lon, gm_utm_t *out);

/**
 * Forward Lambert Conformal Conic (2SP) transform for NAD83(1986) North
 * Dakota State Plane, North Zone (EPSG:2265). Forward-only -- no inverse
 * transform exists, since nothing in GeoMark currently re-ingests a
 * previously-exported State Plane coordinate (gm_point_t stores WGS84
 * lat/lon/altitude natively; State Plane is a display/export-time
 * conversion only).
 *
 * Valid input range: this zone's two standard parallels are
 * 47°26'N and 48°44'N: accuracy degrades the further a point is from
 * that band (the zone is intended for use within North Dakota's North
 * Zone counties only -- see ND Century Code ch. 47-20.2 for the zone's
 * county boundaries). No range check is performed here; the projection
 * math itself remains well-defined for any input, it simply becomes a
 * poor real-world approximation far outside the zone.
 *
 * Returns GM_ERR_GENERIC if lat/lon are NaN or out is NULL.
 */
gm_status_t coords_wgs84_to_nd_north(double lat, double lon, gm_state_plane_t *out);

#endif /* GEOMARK_COORDS_H */