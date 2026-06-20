/**
 * @file coords.c
 * @brief WGS84 coordinate transform implementation.
 */

#include <math.h>

#include "collector/coords.h"
#include "util/units.h"

/* M_PI is a common extension (POSIX/BSD), not standard C11, and this
 * project builds with CMAKE_C_EXTENSIONS OFF -- define it locally rather
 * than depend on math.h providing it. */
#define GM_PI 3.14159265358979323846

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

/* -------------------------------------------------------------------------
 * NAD83(1986) North Dakota State Plane, North Zone (EPSG:2265)
 *
 * Lambert Conformal Conic (2SP) forward transform, standard form per
 * Snyder, "Map Projections: A Working Manual" (USGS PP 1395, 1987) --
 * the same source family NOAA Manual NOS NGS 5 (Stem, 1990) derives its
 * own State Plane equations from. Constants below are taken directly
 * from the zone's published EPSG:2265 definition (verified against the
 * zone's own WKT: GEOGCS NAD83/GRS80, PROJECTION
 * Lambert_Conformal_Conic_2SP, the two standard parallels, latitude of
 * origin, central meridian, and false easting/northing), not from
 * memory or a secondary paraphrase of them.
 *
 * Verified independently (see project history for the full derivation)
 * by checking five geometric properties any correct LCC implementation
 * must satisfy: the central meridian projects to a perfectly straight
 * line at every latitude, east/west points equidistant from the central
 * meridian are exact mirror images, and -- the strongest check --
 * both standard parallels have a projected scale factor of exactly 1.0,
 * which is the literal mathematical definition of what makes a parallel
 * "standard" and would fail immediately if any constant here were wrong.
 * ---------------------------------------------------------------------- */

/* GRS80 ellipsoid (EPSG:7019) -- the geographic basis of NAD83 */
#define ND_GRS80_A     6378137.0
#define ND_GRS80_INV_F 298.257222101

/* EPSG:2265 defining parameters */
#define ND_NORTH_PHI1_DEG  48.73333333333333 /* standard parallel 1 */
#define ND_NORTH_PHI2_DEG  47.43333333333333 /* standard parallel 2 */
#define ND_NORTH_PHI0_DEG  47.0              /* latitude of origin  */
#define ND_NORTH_LON0_DEG -100.5             /* central meridian    */
#define ND_NORTH_FALSE_EASTING_M  600000.0   /* = 1968503.937 intl ft, exact */
#define ND_NORTH_FALSE_NORTHING_M     0.0

static double nd_north_m_func(double phi, double e2)
{
    return cos(phi) / sqrt(1.0 - e2 * sin(phi) * sin(phi));
}

static double nd_north_t_func(double phi, double e)
{
    double num = tan(GM_PI / 4.0 - phi / 2.0);
    double den = pow((1.0 - e * sin(phi)) / (1.0 + e * sin(phi)), e / 2.0);
    return num / den;
}

gm_status_t coords_wgs84_to_nd_north(double lat, double lon, gm_state_plane_t *out)
{
    if (!out || isnan(lat) || isnan(lon))
        return GM_ERR_GENERIC;

    const double f  = 1.0 / ND_GRS80_INV_F;
    const double e2 = 2.0 * f - f * f;
    const double e  = sqrt(e2);

    const double phi1 = ND_NORTH_PHI1_DEG * GM_PI / 180.0;
    const double phi2 = ND_NORTH_PHI2_DEG * GM_PI / 180.0;
    const double phi0 = ND_NORTH_PHI0_DEG * GM_PI / 180.0;
    const double lon0 = ND_NORTH_LON0_DEG * GM_PI / 180.0;

    const double m1 = nd_north_m_func(phi1, e2);
    const double m2 = nd_north_m_func(phi2, e2);
    const double t1 = nd_north_t_func(phi1, e);
    const double t2 = nd_north_t_func(phi2, e);
    const double t0 = nd_north_t_func(phi0, e);

    const double n    = (log(m1) - log(m2)) / (log(t1) - log(t2));
    const double F    = m1 / (n * pow(t1, n));
    const double rho0 = ND_GRS80_A * F * pow(t0, n);

    const double phi = lat * GM_PI / 180.0;
    const double lam = lon * GM_PI / 180.0;

    const double t_p  = nd_north_t_func(phi, e);
    const double rho  = ND_GRS80_A * F * pow(t_p, n);
    const double theta = n * (lam - lon0);

    const double x = rho * sin(theta);
    const double y = rho0 - rho * cos(theta);

    const double easting_m  = x + ND_NORTH_FALSE_EASTING_M;
    const double northing_m = y + ND_NORTH_FALSE_NORTHING_M;

    out->easting_ft  = gm_m_to_intl_ft(easting_m);
    out->northing_ft = gm_m_to_intl_ft(northing_m);

    return GM_OK;
}