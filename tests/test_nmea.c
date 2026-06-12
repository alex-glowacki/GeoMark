/* tests/test_nmea.c */
#include "gnss/nmea.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Tolerance for floating-point degree comparisons (~0.1 mm at equator) */
#define DEG_TOL 1e-6

/* -------------------------------------------------------------------------
 * Checksum tests
 * ------------------------------------------------------------------------- */

static void test_checksum_valid(void)
{
    assert(nmea_checksum_valid(
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77"));
    assert(nmea_checksum_valid(
        "$GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*5A"));
    printf("  PASS  checksum valid on known-good sentences\n");
}

static void test_checksum_invalid(void)
{
    /* Wrong checksum byte (correct is *77, using *78) */
    assert(!nmea_checksum_valid(
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*78"));
    /* Missing '*' delimiter */
    assert(!nmea_checksum_valid(
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
    /* No leading '$' */
    assert(!nmea_checksum_valid(
        "GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77"));
    printf("  PASS  checksum rejected on corrupted sentences\n");
}

/* -------------------------------------------------------------------------
 * GGA tests
 * ------------------------------------------------------------------------- */

static void test_parse_gga(void)
{
    NmeaGga gga = {0};
    assert(nmea_parse_gga(
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77",
        &gga) && gga.valid);

    /* 4807.038 N  →  48 + 7.038/60 = 48.117300° */
    assert(fabs(gga.lat - (48.0 + 7.038 / 60.0)) < DEG_TOL);
    /* 01131.000 E →  11 + 31.000/60 = 11.516667° */
    assert(fabs(gga.lon - (11.0 + 31.000 / 60.0)) < DEG_TOL);

    assert(gga.fix_quality == 1);
    assert(gga.num_sats    == 8);
    assert(fabs(gga.hdop    - 0.9)   < DEG_TOL);
    assert(fabs(gga.alt_msl - 545.4) < DEG_TOL);

    printf("  PASS  GGA parse: lat=%.6f lon=%.6f fix=%d sats=%d "
           "hdop=%.1f alt_msl=%.1fm\n",
           gga.lat, gga.lon, gga.fix_quality,
           gga.num_sats, gga.hdop, gga.alt_msl);
}

static void test_parse_gga_south_west(void)
{
    /* Southern + Western hemisphere — signs must flip.
     * Struct is stack-allocated; fields are read by nmea_parse_gga()
     * so we declare it volatile to prevent the Release-mode unused warning. */
    NmeaGga gga = {0};
    int ok = nmea_parse_gga(
        "$GNGGA,000000.00,3340.000,S,07020.000,W,1,06,1.2,100.0,M,0.0,M,,*48",
        &gga);
    if (!ok || !gga.valid || gga.lat >= 0.0 || gga.lon >= 0.0) {
        printf("  FAIL  GGA parse: S/W hemisphere signs incorrect\n");
        assert(0);
    }
    printf("  PASS  GGA parse: S/W hemisphere signs correct\n");
}

static void test_parse_gga_bad_checksum(void)
{
    NmeaGga gga = {0};
    int ok = nmea_parse_gga(
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*78",
        &gga);
    if (ok || gga.valid) {
        printf("  FAIL  GGA parse: accepted bad checksum\n");
        assert(0);
    }
    printf("  PASS  GGA parse: rejected on bad checksum\n");
}

/* -------------------------------------------------------------------------
 * RMC tests
 * ------------------------------------------------------------------------- */

static void test_parse_rmc(void)
{
    NmeaRmc rmc = {0};
    assert(nmea_parse_rmc(
        "$GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*5A",
        &rmc) && rmc.valid && rmc.active);

    assert(fabs(rmc.lat         - (48.0 + 7.038  / 60.0)) < DEG_TOL);
    assert(fabs(rmc.lon         - (11.0 + 31.000 / 60.0)) < DEG_TOL);
    assert(fabs(rmc.speed_knots - 22.4) < DEG_TOL);
    assert(fabs(rmc.course_deg  - 84.4) < DEG_TOL);

    printf("  PASS  RMC parse: lat=%.6f lon=%.6f speed=%.1f kts "
           "course=%.1f° active=%d\n",
           rmc.lat, rmc.lon, rmc.speed_knots, rmc.course_deg, rmc.active);
}

static void test_parse_rmc_void(void)
{
    /* Status 'V' — void, position unreliable, active must be false */
    NmeaRmc rmc = {0};
    int ok = nmea_parse_rmc(
        "$GNRMC,123519.00,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*4D",
        &rmc);
    if (!ok || !rmc.valid || rmc.active) {
        printf("  FAIL  RMC parse: void status handling incorrect\n");
        assert(0);
    }
    printf("  PASS  RMC parse: void status sets active=false\n");
}

/* -------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    printf("=== NMEA Parser Tests ===\n");
    test_checksum_valid();
    test_checksum_invalid();
    test_parse_gga();
    test_parse_gga_south_west();
    test_parse_gga_bad_checksum();
    test_parse_rmc();
    test_parse_rmc_void();
    printf("=== All NMEA tests passed ===\n");
    return 0;
}