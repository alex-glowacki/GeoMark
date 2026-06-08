/**
 * @file coords.c
 * @brief WGS84 coordinate transform implementation.
 */

#include "collector/coords.h"

gm_status_t coords_wgs84_to_ecef(double lat, double lon, double alt, gm_ecef_t *out)
{
    /* TODO: Phase 3 implementation */
    (void)lat;
    (void)lon;
    (void)alt;
    (void)out;
    return GM_ERR_GENERIC;
}

gm_status_t coords_wgs84_to_utm(double lat, double lon, gm_utm_t *out)
{
    /* TODO: Phase 3 implementation */
    (void)lat;
    (void)lon;
    (void)out;
    return GM_ERR_GENERIC;
}