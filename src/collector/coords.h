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

gm_status_t coords_wgs84_to_ecef(double lat, double lon, double alt, gm_ecef_t *out);
gm_status_t coords_wgs84_to_utm(double lat, double lon, gm_utm_t *out);

#endif /* GEOMARK_COORDS_H */