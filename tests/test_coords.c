#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "collector/coords.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_widget.c)
 * ========================================================================= */
static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)                                                     \
    do {                                                                      \
        g_tests_run++;                                                       \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg));  \
            g_tests_failed++;                                                \
        }                                                                    \
    } while (0)

#define NEAR(a, b, tol) (fabs((a) - (b)) < (tol))

/* =========================================================================
 * Reference point: lat=46.8083, lon=-100.7837. Cross-checked independently
 * in Python against the same EPSG:2265 constants before this C
 * implementation was written -- see project history for the full
 * derivation and the five geometric-property checks (central meridian is
 * a straight line at every latitude, east/west symmetry, and scale
 * factor exactly 1.0 at both standard parallels) that gave confidence
 * the constants and formula are correct before porting to C.
 * ========================================================================= */

static void test_known_reference_point(void)
{
    gm_state_plane_t sp;
    gm_status_t rc = coords_wgs84_to_nd_north(46.8083, -100.7837, &sp);

    ASSERT(rc == GM_OK, "Transform succeeds for an in-zone point");
    ASSERT(NEAR(sp.easting_ft, 1897447.454977, 0.001),
          "Easting matches the independently-verified Python reference value");
    ASSERT(NEAR(sp.northing_ft, -69797.606374, 0.001),
          "Northing matches the independently-verified Python reference value");
}

static void test_origin_point(void)
{
    gm_state_plane_t sp;
    gm_status_t rc = coords_wgs84_to_nd_north(47.0, -100.5, &sp);

    ASSERT(rc == GM_OK, "Transform succeeds at the projection origin");
    ASSERT(NEAR(sp.easting_ft, 1968503.937, 0.001),
          "Easting at the origin equals the zone's published false easting exactly");
    ASSERT(NEAR(sp.northing_ft, 0.0, 0.001),
          "Northing at the origin is exactly zero");
}

static void test_central_meridian_is_straight(void)
{
    double test_lats[] = { 46.0, 47.5, 48.7, 49.0 };

    for (size_t i = 0; i < sizeof(test_lats) / sizeof(test_lats[0]); i++) {
        gm_state_plane_t sp;
        coords_wgs84_to_nd_north(test_lats[i], -100.5, &sp);
        double x = sp.easting_ft - 1968503.937; /* remove false easting */
        ASSERT(NEAR(x, 0.0, 0.001),
              "Point on the central meridian has x == 0 at this latitude");
    }
}

static void test_east_west_symmetry(void)
{
    gm_state_plane_t east, west;
    coords_wgs84_to_nd_north(47.0, -99.0, &east);  /* 1.5 deg east */
    coords_wgs84_to_nd_north(47.0, -102.0, &west); /* 1.5 deg west */

    double dx_east = east.easting_ft - 1968503.937;
    double dx_west = west.easting_ft - 1968503.937;

    ASSERT(NEAR(fabs(dx_east), fabs(dx_west), 0.01),
          "East and west points equidistant from the central meridian have equal |x|");
    ASSERT(NEAR(east.northing_ft, west.northing_ft, 0.01),
          "East and west points at the same latitude have equal northing");
    ASSERT((dx_east > 0 && dx_west < 0) || (dx_east < 0 && dx_west > 0),
          "East and west points are on opposite sides of the central meridian");
}

static void test_null_and_nan_guards(void)
{
    gm_status_t rc = coords_wgs84_to_nd_north(47.0, -100.5, NULL);
    ASSERT(rc == GM_ERR_GENERIC, "NULL out pointer returns GM_ERR_GENERIC");

    gm_state_plane_t sp;
    double nan_val = NAN;

    rc = coords_wgs84_to_nd_north(nan_val, -100.5, &sp);
    ASSERT(rc == GM_ERR_GENERIC, "NaN latitude returns GM_ERR_GENERIC");

    rc = coords_wgs84_to_nd_north(47.0, nan_val, &sp);
    ASSERT(rc == GM_ERR_GENERIC, "NaN longitude returns GM_ERR_GENERIC");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_known_reference_point();
    test_origin_point();
    test_central_meridian_is_straight();
    test_east_west_symmetry();
    test_null_and_nan_guards();

    if (g_tests_failed == 0) {
        printf("All %d coordinate transform tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d coordinate transform tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}