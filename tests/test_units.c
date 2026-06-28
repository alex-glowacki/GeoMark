#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/util/units.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_widget.c / test_keyboard.c)
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

/* =========================================================================
 * gm_deg_to_dms_d()/_m()/_s(): always non-negative, regardless of sign.
 *
 * This is the actual bug fix -- before this session, these three
 * functions operated on the signed input directly (no fabs()), so a
 * negative decimal-degrees value (any West longitude or South latitude)
 * produced a negative degrees field AND negative minutes/seconds layered
 * on top of it, e.g. -97.0329 -> (-97, -1, -58.44) instead of the
 * correct (97, 1, 58.44) with the sign/hemisphere tracked separately.
 * Every assertion below using a negative input is a direct regression
 * test for that bug; the positive-input assertions confirm the fix
 * didn't change already-correct behavior for the common case.
 * ========================================================================= */

static void test_dms_magnitude_positive_input(void)
{
    /* 47.9253 deg -> 47 deg, 55 min, ~31.08 sec (matches this session's
     * Measure Points readout work, same lat used in
     * tests/test_screens.c's TestFeedState fixtures). */
    double lat = 47.9253;
    ASSERT(gm_deg_to_dms_d(lat) == 47, "Positive input: degrees magnitude is correct");
    ASSERT(gm_deg_to_dms_m(lat) == 55, "Positive input: minutes magnitude is correct");
    ASSERT(fabs(gm_deg_to_dms_s(lat) - 31.08) < 0.01,
          "Positive input: seconds magnitude is correct to 2 decimal places");
}

static void test_dms_magnitude_negative_input_is_positive(void)
{
    /* -97.0329 deg (a West longitude) -- the exact regression case. */
    double lon = -97.0329;
    ASSERT(gm_deg_to_dms_d(lon) == 97,
          "Negative input: degrees is the POSITIVE magnitude (97, not -97)");
    ASSERT(gm_deg_to_dms_m(lon) == 1,
          "Negative input: minutes is the POSITIVE magnitude (1, not -1)");
    ASSERT(gm_deg_to_dms_s(lon) >= 0.0,
          "Negative input: seconds is never negative");
    ASSERT(fabs(gm_deg_to_dms_s(lon) - 58.44) < 0.01,
          "Negative input: seconds magnitude is correct to 2 decimal places");
}

static void test_dms_degree_boundary_negative(void)
{
    /* Exactly -1.0 deg -- a boundary case where the fractional part is
     * zero, chosen specifically because it's the simplest input that
     * would have produced 0 fractional minutes/seconds either way (a
     * weaker test could pass on the old buggy code by coincidence if it
     * only checked the degrees field here); checking minutes and seconds
     * are also exactly zero, not just non-negative, closes that gap. */
    double deg = -1.0;
    ASSERT(gm_deg_to_dms_d(deg) == 1, "Exactly -1.0 deg: degrees magnitude is 1");
    ASSERT(gm_deg_to_dms_m(deg) == 0, "Exactly -1.0 deg: minutes is exactly 0");
    ASSERT(fabs(gm_deg_to_dms_s(deg) - 0.0) < 1e-9,
          "Exactly -1.0 deg: seconds is exactly 0.0");
}

static void test_dms_zero_input(void)
{
    ASSERT(gm_deg_to_dms_d(0.0) == 0, "Zero input: degrees is 0");
    ASSERT(gm_deg_to_dms_m(0.0) == 0, "Zero input: minutes is 0");
    ASSERT(fabs(gm_deg_to_dms_s(0.0)) < 1e-9, "Zero input: seconds is 0.0");
}

/* =========================================================================
 * gm_lat_bearing() / gm_lon_bearing(): hemisphere letter from sign.
 * ========================================================================= */

static void test_lat_bearing(void)
{
    ASSERT(gm_lat_bearing(47.9253) == 'N', "Positive latitude reports 'N'");
    ASSERT(gm_lat_bearing(-33.8) == 'S', "Negative latitude reports 'S'");
    ASSERT(gm_lat_bearing(0.0) == 'N', "Zero latitude reports 'N' (documented arbitrary default)");
}

static void test_lon_bearing(void)
{
    ASSERT(gm_lon_bearing(151.2) == 'E', "Positive longitude reports 'E'");
    ASSERT(gm_lon_bearing(-97.0329) == 'W', "Negative longitude reports 'W'");
    ASSERT(gm_lon_bearing(0.0) == 'E', "Zero longitude reports 'E' (documented arbitrary default)");
}

/* =========================================================================
 * gm_format_dms_bearing(): the full one-call string formatter Measure
 * Points' draw code actually calls.
 * ========================================================================= */

static void test_format_dms_bearing_latitude(void)
{
    char buf[64];
    int  n = gm_format_dms_bearing(47.9253, true, buf, sizeof(buf));
    ASSERT(n > 0, "gm_format_dms_bearing() returns a positive write count on success");
    ASSERT(strcmp(buf, "47d55'31.08\"N") == 0,
          "Latitude formats as \"47d55'31.08\\\"N\" (degree letter, no raw negative sign anywhere)");
}

static void test_format_dms_bearing_longitude_negative(void)
{
    /* The actual end-to-end regression case: a negative (West) longitude
     * must format with a positive degrees/minutes/seconds triple and a
     * trailing 'W', never a leading '-' anywhere in the string. */
    char buf[64];
    gm_format_dms_bearing(-97.0329, false, buf, sizeof(buf));
    ASSERT(strcmp(buf, "97d1'58.44\"W") == 0,
          "Negative longitude formats as \"97d1'58.44\\\"W\", not \"-97d-1'-58.44\\\"E\"");
    ASSERT(strchr(buf, '-') == NULL,
          "The formatted string contains no '-' character anywhere -- "
          "sign is carried entirely by the trailing hemisphere letter");
}

static void test_format_dms_bearing_truncation_safety(void)
{
    /* A too-small buffer must not overflow -- snprintf()'s own
     * documented truncation behavior, exercised here so a future change
     * to this function that accidentally swaps in a non-bounded
     * formatter (sprintf(), strcat(), etc.) would be caught. */
    char tiny[4];
    int n = gm_format_dms_bearing(47.9253, true, tiny, sizeof(tiny));
    ASSERT(n > (int)sizeof(tiny) - 1,
          "snprintf()'s return value reports the untruncated length even when the "
          "buffer was too small (its own documented contract)");
    ASSERT(tiny[sizeof(tiny) - 1] == '\0',
          "The truncated buffer is still NUL-terminated, not overrun");
}

/* =========================================================================
 * Existing unit conversions (gm_m_to_intl_ft() etc.) -- not new this
 * session, but previously had no dedicated test file at all; covered
 * here now that units.h has a real test suite, against the exact
 * constants units.h itself defines (independent computation, not just
 * calling the function under test and checking it agrees with itself).
 * ========================================================================= */

static void test_intl_foot_conversion(void)
{
    ASSERT(fabs(gm_intl_ft_to_m(1.0) - 0.3048) < 1e-9,
          "1 international foot is exactly 0.3048 m");
    ASSERT(fabs(gm_m_to_intl_ft(0.3048) - 1.0) < 1e-9,
          "0.3048 m is exactly 1 international foot");
    ASSERT(fabs(gm_intl_ft_to_m(gm_m_to_intl_ft(123.456)) - 123.456) < 1e-9,
          "Round-tripping m -> ft -> m recovers the original value");
}

static void test_survey_foot_conversion(void)
{
    double survey_ft_in_m = 1200.0 / 3937.0;
    ASSERT(fabs(gm_survey_ft_to_m(1.0) - survey_ft_in_m) < 1e-12,
          "1 US survey foot is exactly 1200/3937 m");
    ASSERT(fabs(gm_m_to_survey_ft(survey_ft_in_m) - 1.0) < 1e-9,
          "1200/3937 m is exactly 1 US survey foot");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_dms_magnitude_positive_input();
    test_dms_magnitude_negative_input_is_positive();
    test_dms_degree_boundary_negative();
    test_dms_zero_input();
    test_lat_bearing();
    test_lon_bearing();
    test_format_dms_bearing_latitude();
    test_format_dms_bearing_longitude_negative();
    test_format_dms_bearing_truncation_safety();
    test_intl_foot_conversion();
    test_survey_foot_conversion();

    if (g_tests_failed == 0) {
        printf("All %d units tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d units tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}