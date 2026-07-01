/**
 * @file tests/test_measure_points.c
 * @brief Unit tests for collector/measure_points.c's CSV persistence --
 *        specifically the V1 (12-column) / V2 (15-column,
 *        dn_ft/de_ft/dz_ft added) header compatibility introduced for
 *        the Localize feature (see measure_points_screen.h's file-level
 *        "Localize" doc comment). measure_points_project()'s own
 *        projection math is covered by test_coords.c/test_measure_
 *        points_export.c already; this suite is specifically about the
 *        store/CSV round trip, not projection.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "collector/measure_points.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_breaklines.c / test_keyboard.c)
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

#define TEST_CSV_PATH "/tmp/geomark_test_measure_points.csv"

static void cleanup(void)
{
    unlink(TEST_CSV_PATH);
}

static void make_point(MeasurePoint *pt, double lat, double lon,
                       const char *name, const char *code)
{
    memset(pt, 0, sizeof(*pt));
    pt->lat = lat;
    pt->lon = lon;
    pt->alt = 10.0;
    pt->raw_alt = 10.0;
    strncpy(pt->name, name, sizeof(pt->name) - 1);
    strncpy(pt->code, code, sizeof(pt->code) - 1);
}

/* =========================================================================
 * V2 round trip: dn_ft/de_ft/dz_ft survive a save/load cycle exactly.
 * ========================================================================= */

static void test_v2_round_trip_preserves_localization_fields(void)
{
    cleanup();

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, "1", "CP");
    pt.dn_ft = 1.234;
    pt.de_ft = -5.678;
    pt.dz_ft = 0.42;
    measure_points_add(&store, pt);

    ASSERT(measure_points_rewrite_csv(TEST_CSV_PATH, &store) == GM_OK,
          "V2 rewrite succeeds");

    MeasurePointStore loaded;
    ASSERT(measure_points_load_csv(TEST_CSV_PATH, &loaded) == GM_OK,
          "V2 load succeeds");
    ASSERT(loaded.count == 1, "One point round-trips");
    if (loaded.count == 1) {
        ASSERT(loaded.points[0].dn_ft > 1.233 && loaded.points[0].dn_ft < 1.235,
              "dn_ft survives the round trip");
        ASSERT(loaded.points[0].de_ft > -5.679 && loaded.points[0].de_ft < -5.677,
              "de_ft survives the round trip");
        ASSERT(loaded.points[0].dz_ft > 0.419 && loaded.points[0].dz_ft < 0.421,
              "dz_ft survives the round trip");
        ASSERT(strcmp(loaded.points[0].code, "CP") == 0,
              "Ordinary fields (code) still round-trip correctly alongside "
              "the new columns");
    }

    cleanup();
}

/* =========================================================================
 * Backward compatibility: a V1 (pre-localization) file loads cleanly,
 * with every point's correction defaulting to exactly 0.0 -- correct,
 * since no correction could have existed before this feature did.
 * ========================================================================= */

static void test_v1_file_loads_with_zero_correction(void)
{
    cleanup();

    FILE *f = fopen(TEST_CSV_PATH, "w");
    ASSERT(f != NULL, "Test fixture V1 file opens for writing");
    if (!f) return;
    fputs("point_num,timestamp,lat,lon,alt,raw_alt,target_height_m,fix_quality,"
          "hdop,num_sats,name,code\n", f);
    fputs("1,1700000000,46.80830000,-100.78370000,10.000,10.000,0.000,4,0.80,18,1,CP\n", f);
    fclose(f);

    MeasurePointStore store;
    gm_status_t rc = measure_points_load_csv(TEST_CSV_PATH, &store);
    ASSERT(rc == GM_OK, "A V1-format file loads successfully (not GM_ERR_PARSE)");
    ASSERT(store.count == 1, "The one V1 row loads");
    if (store.count == 1) {
        ASSERT(store.points[0].dn_ft == 0.0, "dn_ft defaults to 0.0 for a V1 row");
        ASSERT(store.points[0].de_ft == 0.0, "de_ft defaults to 0.0 for a V1 row");
        ASSERT(store.points[0].dz_ft == 0.0, "dz_ft defaults to 0.0 for a V1 row");
        ASSERT(strcmp(store.points[0].code, "CP") == 0,
              "V1 row's ordinary fields (code) still parse correctly");
        ASSERT(store.points[0].point_num == 1, "V1 row's point_num still parses correctly");
    }

    cleanup();
}

/* =========================================================================
 * Writing always upgrades to V2: rewriting a store loaded from a V1
 * file produces a file with the V2 header, not V1 -- this is the
 * mechanism measure_points_screen.c's reload_job_data() relies on to
 * transparently upgrade an existing job's points.csv.
 * ========================================================================= */

static void test_rewrite_always_writes_v2_header(void)
{
    cleanup();

    FILE *f = fopen(TEST_CSV_PATH, "w");
    ASSERT(f != NULL, "Test fixture V1 file opens for writing");
    if (!f) return;
    fputs("point_num,timestamp,lat,lon,alt,raw_alt,target_height_m,fix_quality,"
          "hdop,num_sats,name,code\n", f);
    fputs("1,1700000000,46.80830000,-100.78370000,10.000,10.000,0.000,4,0.80,18,1,CP\n", f);
    fclose(f);

    MeasurePointStore store;
    measure_points_load_csv(TEST_CSV_PATH, &store);
    ASSERT(measure_points_rewrite_csv(TEST_CSV_PATH, &store) == GM_OK,
          "Rewriting a V1-loaded store succeeds");

    FILE *check = fopen(TEST_CSV_PATH, "r");
    ASSERT(check != NULL, "Rewritten file opens for a header check");
    if (check) {
        char header[256];
        ASSERT(fgets(header, sizeof(header), check) != NULL,
              "Rewritten file has a header line");
        ASSERT(strstr(header, "dn_ft") != NULL,
              "The rewritten file's header is V2 (contains dn_ft), even "
              "though it was loaded from a V1 file");
        fclose(check);
    }

    /* Loading it back a second time must now take the V2 branch and
     * still find the same (zero) correction values, proving the
     * upgraded file round-trips correctly too. */
    MeasurePointStore reloaded;
    ASSERT(measure_points_load_csv(TEST_CSV_PATH, &reloaded) == GM_OK,
          "The upgraded V2 file loads back successfully");
    ASSERT(reloaded.count == 1 && reloaded.points[0].dn_ft == 0.0,
          "The upgraded file's point still has dn_ft == 0.0");

    cleanup();
}

/* =========================================================================
 * append_csv on a brand-new file writes the V2 header.
 * ========================================================================= */

static void test_append_csv_new_file_writes_v2_header(void)
{
    cleanup();

    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, "1", "CP");
    pt.point_num = 1;
    ASSERT(measure_points_append_csv(TEST_CSV_PATH, &pt) == GM_OK,
          "Appending to a brand-new path succeeds");

    FILE *f = fopen(TEST_CSV_PATH, "r");
    ASSERT(f != NULL, "Newly-created file opens for a header check");
    if (f) {
        char header[256];
        ASSERT(fgets(header, sizeof(header), f) != NULL, "File has a header line");
        ASSERT(strstr(header, "dn_ft") != NULL,
              "A brand-new file's header is V2 (contains dn_ft)");
        fclose(f);
    }

    cleanup();
}

/* =========================================================================
 * A header matching neither known format is still rejected.
 * ========================================================================= */

static void test_unrecognized_header_is_rejected(void)
{
    cleanup();

    FILE *f = fopen(TEST_CSV_PATH, "w");
    ASSERT(f != NULL, "Test fixture opens for writing");
    if (!f) return;
    fputs("this,is,not,a,real,header\n", f);
    fclose(f);

    MeasurePointStore store;
    ASSERT(measure_points_load_csv(TEST_CSV_PATH, &store) == GM_ERR_PARSE,
          "A header matching neither V1 nor V2 is rejected with GM_ERR_PARSE");

    cleanup();
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_v2_round_trip_preserves_localization_fields();
    test_v1_file_loads_with_zero_correction();
    test_rewrite_always_writes_v2_header();
    test_append_csv_new_file_writes_v2_header();
    test_unrecognized_header_is_rejected();

    if (g_tests_failed == 0) {
        printf("All %d measure_points tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d measure_points tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}