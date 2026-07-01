/**
 * @file test_measure_points_export.c
 * @brief Tests for collector/measure_points_export.c (LandXML and PNEZD
 *        CSV export) and the measure_points_remove()/rewrite_csv()
 *        store-mutation functions added in the same session.
 *
 * All tests are pure host-testable (no networking, no hardware) --
 * same "keep live OS state out of unit tests" convention this codebase
 * applies throughout (see usb_export.h's file-level doc comment on
 * why is_mounted() itself is not covered here).
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "collector/measure_points_export.h"
#include "util/units.h"

/* =========================================================================
 * Minimal test harness
 * ========================================================================= */
static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)                                                     \
    do {                                                                      \
        g_tests_run++;                                                        \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
            g_tests_failed++;                                                 \
        }                                                                     \
    } while (0)

#define NEAR(a, b, tol) (fabs((a) - (b)) < (tol))

/* =========================================================================
 * Helpers
 * ========================================================================= */

static bool slurp_file(const char *path, char *buf, size_t buf_len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    size_t n = fread(buf, 1, buf_len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

static void make_point(MeasurePoint *pt, double lat, double lon, double alt,
                       const char *name, const char *code)
{
    memset(pt, 0, sizeof(*pt));
    pt->lat = lat;
    pt->lon = lon;
    pt->alt = alt;
    pt->raw_alt = alt;
    strncpy(pt->name, name, sizeof(pt->name) - 1);
    strncpy(pt->code, code, sizeof(pt->code) - 1);
}

#define TEST_JOB_DIR "/tmp/geomark_export_test_job"

static void cleanup_test_job_dir(void)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/export/points.xml",        TEST_JOB_DIR); unlink(path);
    snprintf(path, sizeof(path), "%s/export/points_pnezd.csv",  TEST_JOB_DIR); unlink(path);
    snprintf(path, sizeof(path), "%s/export/points_pnezd.txt",  TEST_JOB_DIR); unlink(path);
    snprintf(path, sizeof(path), "%s/points.csv",               TEST_JOB_DIR); unlink(path);
    snprintf(path, sizeof(path), "%s/export",                   TEST_JOB_DIR); rmdir(path);
    rmdir(TEST_JOB_DIR);
}

/* =========================================================================
 * Path helpers
 * ========================================================================= */

static void test_path_helpers(void)
{
    char buf[768];

    measure_points_export_landxml_path(
        "/home/alex/geomark-data/projects/p/j", buf, sizeof(buf));
    ASSERT(strcmp(buf, "/home/alex/geomark-data/projects/p/j/export/points.xml") == 0,
          "LandXML export path is job_dir/export/points.xml");

    measure_points_export_pnezd_path(
        "/home/alex/geomark-data/projects/p/j", buf, sizeof(buf));
    ASSERT(strcmp(buf,
                  "/home/alex/geomark-data/projects/p/j/export/points_pnezd.csv") == 0,
          "PNEZD export path is job_dir/export/points_pnezd.csv");

    measure_points_export_txt_path(
        "/home/alex/geomark-data/projects/p/j", buf, sizeof(buf));
    ASSERT(strcmp(buf,
                  "/home/alex/geomark-data/projects/p/j/export/points_pnezd.txt") == 0,
          "PNEZD TXT export path is job_dir/export/points_pnezd.txt");

    /* Both export paths are distinct from the internal points.csv path. */
    char internal_buf[768];
    measure_points_csv_path("/home/alex/geomark-data/projects/p/j",
                            internal_buf, sizeof(internal_buf));
    ASSERT(strcmp(buf, internal_buf) != 0,
          "PNEZD export path is distinct from the internal points.csv path");
}

/* =========================================================================
 * NULL guards
 * ========================================================================= */

static void test_null_guards(void)
{
    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    ASSERT(measure_points_export_landxml("/tmp/unused.xml", NULL, &meta, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "LandXML export: NULL store -> GM_ERR_GENERIC");
    ASSERT(measure_points_export_landxml("/tmp/unused.xml", &store, NULL, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "LandXML export: NULL job_meta -> GM_ERR_GENERIC");
    ASSERT(measure_points_export_pnezd("/tmp/unused.csv", NULL, &meta, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "PNEZD export: NULL store -> GM_ERR_GENERIC");
    ASSERT(measure_points_export_pnezd("/tmp/unused.csv", &store, NULL, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "PNEZD export: NULL job_meta -> GM_ERR_GENERIC");
    ASSERT(measure_points_export_txt("/tmp/unused.txt", NULL, &meta, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "PNEZD TXT export: NULL store -> GM_ERR_GENERIC");
    ASSERT(measure_points_export_txt("/tmp/unused.txt", &store, NULL, 0.0, 0.0)
               == GM_ERR_GENERIC,
          "PNEZD TXT export: NULL job_meta -> GM_ERR_GENERIC");
}

/* =========================================================================
 * Export directory auto-creation
 * ========================================================================= */

static void test_creates_export_dir(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    struct stat st;
    ASSERT(stat(TEST_JOB_DIR "/export", &st) != 0,
          "export/ subdirectory does not exist before the first export");

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD export succeeds and creates export/ in one call");
    ASSERT(stat(TEST_JOB_DIR "/export", &st) == 0 && S_ISDIR(st.st_mode),
          "export/ subdirectory exists after the export call");

    cleanup_test_job_dir();
}

/* =========================================================================
 * Empty store -- not an error
 * ========================================================================= */

static void test_empty_store_pnezd(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD export of empty store succeeds");

    char content[256];
    ASSERT(slurp_file(path, content, sizeof(content)), "Empty-store PNEZD file is readable");
    ASSERT(content[0] == '\0',
          "Empty-store PNEZD is a completely empty file (no header row -- Civil 3D "
          "PNEZD format has no header)");

    cleanup_test_job_dir();
}

static void test_empty_store_landxml(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_landxml_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_landxml(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "LandXML export of empty store succeeds");

    char content[2048];
    ASSERT(slurp_file(path, content, sizeof(content)), "Empty-store LandXML is readable");
    ASSERT(strstr(content, "<CgPoints>") && strstr(content, "</CgPoints>"),
          "Empty-store LandXML still has a valid CgPoints element");
    ASSERT(strstr(content, "<CgPoint ") == NULL,
          "Empty-store LandXML has zero CgPoint entries");
    ASSERT(strstr(content, "<?xml version=\"1.0\"") != NULL,
          "LandXML output starts with a valid XML declaration");

    cleanup_test_job_dir();
}

/* =========================================================================
 * PNEZD column order: Point#, Northing, Easting, Elevation, Description
 *
 * This is the only column order Civil 3D's native PNEZD importer
 * accepts. Verified against the ND North reference coordinates
 * test_coords.c independently confirms.
 * ========================================================================= */

static void test_pnezd_nd_north_column_order_and_values(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    /* Reference: lat=46.8083, lon=-100.7837 -> N=-69797.606374,
     * E=1897447.454977 (ft, EPSG:2265). alt=10.0m -> 32.8084 ft. */
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "CP");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD ND North export succeeds");

    char content[2048];
    ASSERT(slurp_file(path, content, sizeof(content)), "PNEZD ND North file is readable");

    /* No header: first line IS the data row. */
    unsigned point_num;
    double northing, easting, elev;
    char desc[64];
    int n = sscanf(content, "%u,%lf,%lf,%lf,%63s",
                   &point_num, &northing, &easting, &elev, desc);
    ASSERT(n == 5, "PNEZD ND North row parses as 5 columns: PNUM,N,E,Z,DESC");
    ASSERT(point_num == 1,          "PNEZD column 1 is point number");
    ASSERT(NEAR(northing, -69797.606374,  0.01),
          "PNEZD column 2 is northing (not easting) in feet");
    ASSERT(NEAR(easting,  1897447.454977, 0.01),
          "PNEZD column 3 is easting in feet");
    ASSERT(NEAR(elev, 32.8084, 0.01),
          "PNEZD column 4 is elevation in feet");
    ASSERT(strstr(desc, "CP") != NULL,
          "PNEZD column 5 is Description (point code)");

    cleanup_test_job_dir();
}

static void test_pnezd_no_header_row(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "BM");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0);

    char content[2048];
    slurp_file(path, content, sizeof(content));

    /* If there were a header the first token would be non-numeric text.
     * A pure PNEZD data row starts with the point number (an integer). */
    unsigned first_val = 0;
    int scanned = sscanf(content, "%u,", &first_val);
    ASSERT(scanned == 1 && first_val == 1,
          "PNEZD file first line starts with the point number (no header row)");

    cleanup_test_job_dir();
}

/* =========================================================================
 * Imperial units -- verify the local-fallback (non-ND-North) path also
 * converts metres to feet, NOT passes raw metres through.
 * ========================================================================= */

static void test_pnezd_local_fallback_imperial(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_LOCAL_GROUND;
    meta.dist_unit = GM_DIST_UNIT_US_SURVEY_FOOT;

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint origin_pt, offset_pt;
    make_point(&origin_pt, 47.0, -97.0, 100.0, "1", "CP");
    make_point(&offset_pt, 47.0001, -97.0, 100.0, "2", "TP");
    measure_points_add(&store, origin_pt);
    measure_points_add(&store, offset_pt);

    /* Independently compute expected values. */
    MeasurePointsProjected proj;
    measure_points_project(&meta, offset_pt.lat, offset_pt.lon,
                           origin_pt.lat, origin_pt.lon, &proj);
    double expected_n_ft = gm_m_to_survey_ft(proj.north);
    double expected_e_ft = gm_m_to_survey_ft(proj.east);
    double expected_z_ft = gm_m_to_intl_ft(100.0);

    /* The raw metre values from measure_points_project() are ~11.13m
     * northing for 0.0001 deg at ~47 deg N. In feet that's ~36.5 ft.
     * If the export were passing raw metres, we would see ~11 -- check
     * that exported values match feet, not metres. */
    ASSERT(expected_n_ft > 30.0,
          "Test precondition: expected northing is clearly >30 ft (would be ~11 in metres)");

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, origin_pt.lat, origin_pt.lon)
               == GM_OK,
          "Local-fallback PNEZD export succeeds");

    char content[2048];
    slurp_file(path, content, sizeof(content));

    /* Read the second data row (offset point). */
    char *row2 = strchr(content, '\n');
    ASSERT(row2 != NULL, "PNEZD file has at least two rows");
    if (row2) {
        row2++;
        unsigned pnum;
        double n, e, z;
        char desc[32];
        sscanf(row2, "%u,%lf,%lf,%lf,%31s", &pnum, &n, &e, &z, desc);
        ASSERT(NEAR(n, expected_n_ft, 0.01),
              "Local-fallback PNEZD northing is in feet (US Survey), not raw metres");
        ASSERT(NEAR(e, expected_e_ft, 0.01),
              "Local-fallback PNEZD easting is in feet (US Survey), not raw metres");
        ASSERT(NEAR(z, expected_z_ft, 0.01),
              "Local-fallback PNEZD elevation is always International Foot");
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * Formula injection safety -- codes starting with +/-/=/@
 *
 * Reproduces the exact bug Alex reported: "+CONC" exported as "+CONC"
 * opens in Excel as a formula ("=-CONC" after autocorrect), producing
 * #NAME?. The fix: prefix a space inside a quoted field.
 * ========================================================================= */

static void test_pnezd_formula_injection_safety(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);

    /* The four trigger characters: '+', '-', '=', '@'. */
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "+CONC");
    measure_points_add(&store, pt);
    make_point(&pt, 46.8083, -100.7837, 10.0, "2", "-RCPF");
    measure_points_add(&store, pt);
    make_point(&pt, 46.8083, -100.7837, 10.0, "3", "=SUM(1,2)");
    measure_points_add(&store, pt);
    make_point(&pt, 46.8083, -100.7837, 10.0, "4", "@LABEL");
    measure_points_add(&store, pt);
    /* A normal code must NOT be quoted (no overhead for the common case). */
    make_point(&pt, 46.8083, -100.7837, 10.0, "5", "CONC");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD export with formula-trigger codes succeeds");

    char content[4096];
    slurp_file(path, content, sizeof(content));

    /* Each injection-trigger code must appear inside a quoted field with
     * a leading space, e.g.: ," +CONC"\n */
    ASSERT(strstr(content, "\" +CONC\"") != NULL,
          "'+CONC' is written as '\" +CONC\"' (quoted with space prefix) in PNEZD");
    ASSERT(strstr(content, "\" -RCPF\"") != NULL,
          "'-RCPF' is written as '\" -RCPF\"' (quoted with space prefix) in PNEZD");
    ASSERT(strstr(content, "\" =SUM(1,2)\"") != NULL,
          "'=SUM(1,2)' is written quoted with space prefix in PNEZD");
    ASSERT(strstr(content, "\" @LABEL\"") != NULL,
          "'@LABEL' is written quoted with space prefix in PNEZD");

    /* The plain code must NOT be wrapped in quotes. */
    ASSERT(strstr(content, "\"CONC\"") == NULL,
          "Normal code 'CONC' is NOT quoted -- no overhead for safe codes");
    ASSERT(strstr(content, ",CONC\n") != NULL,
          "Normal code 'CONC' appears bare, comma-separated, in the output");

    /* None of the trigger codes should appear UNQUOTED (which would
     * trigger formula evaluation in Excel). */
    ASSERT(strstr(content, ",+CONC") == NULL,
          "'+CONC' never appears unquoted after a comma");
    ASSERT(strstr(content, ",-RCPF") == NULL,
          "'-RCPF' never appears unquoted after a comma");

    cleanup_test_job_dir();
}

/* =========================================================================
 * Localization correction: a point's own dn_ft/de_ft/dz_ft (see
 * measure_points.h's MeasurePoint doc comment) must reach the exported
 * Northing/Easting/Elevation, in every export format -- this is the
 * actual mechanism the Measure Points "Localize" feature depends on.
 * Same reference point/values as test_pnezd_nd_north_column_order_and_
 * values, with a correction added on top.
 * ========================================================================= */

static void test_localization_correction_reaches_pnezd_export(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "CP");
    pt.dn_ft = 2.5;
    pt.de_ft = -1.25;
    pt.dz_ft = 0.1;
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD export with a localized point succeeds");

    char content[2048];
    slurp_file(path, content, sizeof(content));

    unsigned point_num;
    double northing, easting, elev;
    char desc[64];
    int n = sscanf(content, "%u,%lf,%lf,%lf,%63s",
                   &point_num, &northing, &easting, &elev, desc);
    ASSERT(n == 5, "PNEZD row still parses as 5 columns with a correction applied");
    /* Uncorrected reference values (from test_pnezd_nd_north_column_
     * order_and_values): N=-69797.606374, E=1897447.454977, Z=32.8084.
     * The correction is additive on top of those. */
    ASSERT(NEAR(northing, -69797.606374 + 2.5, 0.01),
          "Northing includes the point's own +2.5 ft correction");
    ASSERT(NEAR(easting, 1897447.454977 - 1.25, 0.01),
          "Easting includes the point's own -1.25 ft correction");
    ASSERT(NEAR(elev, 32.8084 + 0.1, 0.01),
          "Elevation includes the point's own +0.1 ft correction");

    cleanup_test_job_dir();
}

static void test_localization_correction_is_zero_noop_by_default(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "CP"); /* dn_ft/de_ft/dz_ft
                                                           * left at 0.0 by
                                                           * make_point()'s
                                                           * own memset() */
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_pnezd_path(TEST_JOB_DIR, path, sizeof(path));
    measure_points_export_pnezd(path, &store, &meta, 0.0, 0.0);

    char content[2048];
    slurp_file(path, content, sizeof(content));

    double northing, easting, elev;
    unsigned point_num;
    char desc[64];
    sscanf(content, "%u,%lf,%lf,%lf,%63s", &point_num, &northing, &easting, &elev, desc);
    ASSERT(NEAR(northing, -69797.606374, 0.01),
          "With no correction, Northing matches the uncorrected reference value exactly");
    ASSERT(NEAR(easting, 1897447.454977, 0.01),
          "With no correction, Easting matches the uncorrected reference value exactly");
    ASSERT(NEAR(elev, 32.8084, 0.01),
          "With no correction, Elevation matches the uncorrected reference value exactly");

    cleanup_test_job_dir();
}

/* =========================================================================
 * PNEZD TXT export: header block + CRLF-terminated data rows, matching
 * the shape of the reference export Alex supplied
 * (TOPO_EXPORT_EXAMPLE.txt).
 * ========================================================================= */

static void test_txt_header_and_crlf(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);
    strncpy(meta.job_name, "TESTJOB", sizeof(meta.job_name) - 1);
    strncpy(meta.reference, "1234-56789", sizeof(meta.reference) - 1);
    strncpy(meta.description, "Preliminary Survey", sizeof(meta.description) - 1);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "CP");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_txt_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_txt(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD TXT export succeeds");

    char content[4096];
    ASSERT(slurp_file(path, content, sizeof(content)), "PNEZD TXT file is readable");

    ASSERT(strstr(content, "Name: TESTJOB\r\n") != NULL,
          "Header includes the job name from job_meta");
    ASSERT(strstr(content, "Name: United States/NAD83\r\n") != NULL,
          "Header includes the NAD83 datum-family line for ND North jobs");
    ASSERT(strstr(content, "Zone: North Dakota North 3301\r\n") != NULL,
          "Header includes the ND North zone, matching Alex's own "
          "reference export's zone name exactly");
    ASSERT(strstr(content, "Reference number: 1234-56789\r\n") != NULL,
          "Header includes the reference number from job_meta");
    ASSERT(strstr(content, "Description: Preliminary Survey\r\n") != NULL,
          "Header includes the description from job_meta");
    ASSERT(strstr(content, "Units: International Feet\r\n") != NULL,
          "Header reports International Feet -- ND North's job_metadata_"
          "coerce_units() forces this unit");

    /* Every line in the file (header AND data rows) is CRLF-terminated,
     * never a bare LF -- count occurrences of each. */
    size_t crlf_count = 0, bare_lf_count = 0;
    for (const char *p = content; *p; p++) {
        if (*p == '\n') {
            bare_lf_count++;
            if (p > content && p[-1] == '\r')
                crlf_count++;
        }
    }
    ASSERT(bare_lf_count > 0 && crlf_count == bare_lf_count,
          "Every line ending in the file is CRLF, not a bare LF");

    cleanup_test_job_dir();
}

/* =========================================================================
 * PNEZD TXT export: data rows carry the exact same projected coordinates
 * and Description-column safety as the plain CSV export -- same
 * reference point/values as test_pnezd_nd_north_column_order_and_values.
 * ========================================================================= */

static void test_txt_data_rows_match_csv_values(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "+CONC");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_txt_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_txt(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD TXT export succeeds");

    char content[4096];
    slurp_file(path, content, sizeof(content));

    /* Same reference values test_pnezd_nd_north_column_order_and_values
     * already verifies for the CSV export. */
    unsigned point_num;
    double northing, easting, elev;
    char desc[64];
    const char *row = strstr(content, "1,");
    ASSERT(row != NULL, "The data row for point 1 is present");
    if (!row) { cleanup_test_job_dir(); return; }
    int n = sscanf(row, "%u,%lf,%lf,%lf,%63s",
                   &point_num, &northing, &easting, &elev, desc);
    ASSERT(n == 5, "TXT data row parses as 5 columns: PNUM,N,E,Z,DESC");
    ASSERT(point_num == 1, "TXT column 1 is point number");
    ASSERT(NEAR(northing, -69797.606374, 0.01), "TXT column 2 is northing in feet");
    ASSERT(NEAR(easting,  1897447.454977, 0.01), "TXT column 3 is easting in feet");
    ASSERT(NEAR(elev, 32.8084, 0.01), "TXT column 4 is elevation in feet");
    ASSERT(strstr(row, "\" +CONC\"") != NULL,
          "'+CONC' is quoted with a space prefix in TXT too, same as the CSV export");

    cleanup_test_job_dir();
}

/* =========================================================================
 * PNEZD TXT export: non-ND-North jobs write an honest "no State Plane
 * zone" line instead of fabricating a zone name.
 * ========================================================================= */

static void test_txt_non_nd_north_zone_line(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta); /* GM_COORD_SYS_WGS84 by default */

    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_txt_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_txt(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD TXT export succeeds for a non-ND-North job");

    char content[4096];
    slurp_file(path, content, sizeof(content));

    ASSERT(strstr(content, "North Dakota North 3301") == NULL,
          "A WGS84-geographic job's header does NOT claim the ND North zone");
    ASSERT(strstr(content, "no State Plane zone") != NULL,
          "A WGS84-geographic job's header honestly says it has no "
          "State Plane zone, instead of a fabricated one");

    cleanup_test_job_dir();
}

/* =========================================================================
 * PNEZD TXT export: an empty store still writes the header block, with
 * zero data rows following it.
 * ========================================================================= */

static void test_txt_empty_store(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_txt_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_txt(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "PNEZD TXT export succeeds with zero points");

    char content[4096];
    slurp_file(path, content, sizeof(content));
    ASSERT(strstr(content, "Zone: North Dakota North 3301\r\n") != NULL,
          "Header is still written even with zero points");
    ASSERT(strstr(content, "GRID Coordinates") != NULL,
          "The scale-factor line is still written with zero points");

    cleanup_test_job_dir();
}

/* =========================================================================
 * LandXML content -- attribute escaping, N/E/Z order, imperial values
 * ========================================================================= */

static void test_landxml_point_content(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta);

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    make_point(&pt, 46.8083, -100.7837, 10.0, "BM \"A\"", "EP&PIPE");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_landxml_path(TEST_JOB_DIR, path, sizeof(path));
    ASSERT(measure_points_export_landxml(path, &store, &meta, 0.0, 0.0) == GM_OK,
          "LandXML export with one point succeeds");

    char content[4096];
    slurp_file(path, content, sizeof(content));

    ASSERT(strstr(content, "name=\"BM &quot;A&quot;\"") != NULL,
          "LandXML escapes double-quote in point name");
    ASSERT(strstr(content, "code=\"EP&amp;PIPE\"") != NULL,
          "LandXML escapes ampersand in point code");

    /* Coordinate order: Northing Easting Elevation (LandXML PointType). */
    char *cgpoint = strstr(content, "<CgPoint ");
    ASSERT(cgpoint != NULL, "LandXML output contains a CgPoint element");
    if (cgpoint) {
        char *gt = strchr(cgpoint, '>');
        ASSERT(gt != NULL, "CgPoint tag is well-formed");
        if (gt) {
            double n, e, el;
            sscanf(gt + 1, "%lf %lf %lf", &n, &e, &el);
            ASSERT(NEAR(n,  -69797.606374,  0.01), "LandXML coord[0] is northing (feet)");
            ASSERT(NEAR(e,  1897447.454977, 0.01), "LandXML coord[1] is easting (feet)");
            ASSERT(NEAR(el, 32.8084,        0.01), "LandXML coord[2] is elevation (feet)");
        }
    }

    /* Units element must declare Imperial/foot. */
    ASSERT(strstr(content, "linearUnit=\"foot\"") != NULL ||
           strstr(content, "linearUnit=\"USSurveyFoot\"") != NULL,
          "LandXML <Units> declares a foot-based linear unit");

    cleanup_test_job_dir();
}

/* =========================================================================
 * LandXML comment must not contain "--" (illegal per XML spec)
 * ========================================================================= */

static void test_landxml_no_illegal_comment_sequence(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_landxml_path(TEST_JOB_DIR, path, sizeof(path));
    measure_points_export_landxml(path, &store, &meta, 0.0, 0.0);

    char content[2048];
    slurp_file(path, content, sizeof(content));

    char *comment_start = strstr(content, "<!--");
    char *comment_end   = comment_start ? strstr(comment_start, "-->") : NULL;
    ASSERT(comment_start && comment_end,
          "LandXML output contains a recognizable comment");
    if (comment_start && comment_end) {
        bool found_double_hyphen = false;
        for (char *p = comment_start + 4; p < comment_end - 1; p++) {
            if (p[0] == '-' && p[1] == '-') { found_double_hyphen = true; break; }
        }
        ASSERT(!found_double_hyphen,
              "LandXML comment body contains no '--' (illegal per XML spec)");
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * measure_points_remove() -- in-memory store manipulation
 * ========================================================================= */

static void test_remove_from_middle(void)
{
    MeasurePointStore store;
    measure_points_init(&store);

    MeasurePoint pt;
    make_point(&pt, 46.0, -97.0, 100.0, "1", "CP"); measure_points_add(&store, pt);
    make_point(&pt, 46.1, -97.1, 101.0, "2", "TP"); measure_points_add(&store, pt);
    make_point(&pt, 46.2, -97.2, 102.0, "3", "BM"); measure_points_add(&store, pt);

    /* Remove the middle point (store index 1). */
    ASSERT(measure_points_remove(&store, 1) == GM_OK,
          "remove at valid middle index returns GM_OK");
    ASSERT(store.count == 2, "store count decremented after remove");

    /* Point 1 (name "1") and point 3 (name "3") remain; their
     * point_nums are preserved, not renumbered. */
    ASSERT(store.points[0].point_num == 1, "first remaining point keeps its original point_num");
    ASSERT(store.points[1].point_num == 3, "second remaining point keeps its original point_num");
    ASSERT(strcmp(store.points[0].name, "1") == 0, "first remaining point is original point 1");
    ASSERT(strcmp(store.points[1].name, "3") == 0, "second remaining point is original point 3");
}

static void test_remove_last_element(void)
{
    MeasurePointStore store;
    measure_points_init(&store);

    MeasurePoint pt;
    make_point(&pt, 46.0, -97.0, 100.0, "1", "A"); measure_points_add(&store, pt);
    make_point(&pt, 46.1, -97.1, 101.0, "2", "B"); measure_points_add(&store, pt);

    ASSERT(measure_points_remove(&store, 1) == GM_OK,
          "remove at last valid index (count-1) returns GM_OK");
    ASSERT(store.count == 1, "store count decremented correctly");
    ASSERT(strcmp(store.points[0].name, "1") == 0, "remaining point is the original first point");
}

static void test_remove_only_element(void)
{
    MeasurePointStore store;
    measure_points_init(&store);

    MeasurePoint pt;
    make_point(&pt, 46.0, -97.0, 100.0, "1", "X"); measure_points_add(&store, pt);

    ASSERT(measure_points_remove(&store, 0) == GM_OK,
          "remove from a one-element store returns GM_OK");
    ASSERT(store.count == 0, "store is empty after removing the only element");
}

static void test_remove_out_of_range(void)
{
    MeasurePointStore store;
    measure_points_init(&store);

    MeasurePoint pt;
    make_point(&pt, 46.0, -97.0, 100.0, "1", "CP"); measure_points_add(&store, pt);

    ASSERT(measure_points_remove(&store, 1) == GM_ERR_GENERIC,
          "remove at index == count returns GM_ERR_GENERIC (out of range)");
    ASSERT(measure_points_remove(&store, 99) == GM_ERR_GENERIC,
          "remove at large out-of-range index returns GM_ERR_GENERIC");
    ASSERT(store.count == 1, "store unchanged after failed remove attempts");
}

static void test_remove_null_store(void)
{
    ASSERT(measure_points_remove(NULL, 0) == GM_ERR_GENERIC,
          "remove with NULL store returns GM_ERR_GENERIC");
}

/* =========================================================================
 * measure_points_rewrite_csv() -- on-disk state after remove
 * ========================================================================= */

static void test_rewrite_csv_after_remove(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    /* Build an initial CSV with three points via append (the normal
     * capture path), then remove the middle and rewrite, then reload
     * and confirm the on-disk state matches the in-memory state. */
    MeasurePointStore store;
    measure_points_init(&store);

    char csv_path[600];
    measure_points_csv_path(TEST_JOB_DIR, csv_path, sizeof(csv_path));

    MeasurePoint pt;
    make_point(&pt, 46.0, -97.0, 100.0, "1", "CP"); measure_points_add(&store, pt);
    measure_points_append_csv(csv_path, &store.points[0]);
    make_point(&pt, 46.1, -97.1, 101.0, "2", "TP"); measure_points_add(&store, pt);
    measure_points_append_csv(csv_path, &store.points[1]);
    make_point(&pt, 46.2, -97.2, 102.0, "3", "BM"); measure_points_add(&store, pt);
    measure_points_append_csv(csv_path, &store.points[2]);

    ASSERT(store.count == 3, "three points in store before remove");

    /* Remove middle point (store index 1, original point_num 2). */
    measure_points_remove(&store, 1);
    ASSERT(store.count == 2, "two points in store after remove");

    /* Rewrite CSV. */
    ASSERT(measure_points_rewrite_csv(csv_path, &store) == GM_OK,
          "rewrite_csv after remove succeeds");

    /* Reload into a fresh store and verify. */
    MeasurePointStore reloaded;
    ASSERT(measure_points_load_csv(csv_path, &reloaded) == GM_OK,
          "reloaded CSV after rewrite is valid");
    ASSERT(reloaded.count == 2, "reloaded store has two points");
    ASSERT(reloaded.points[0].point_num == 1, "reloaded point 0 has original point_num 1");
    ASSERT(reloaded.points[1].point_num == 3, "reloaded point 1 has original point_num 3");
    ASSERT(strcmp(reloaded.points[0].name, "1") == 0, "reloaded point 0 name is '1'");
    ASSERT(strcmp(reloaded.points[1].name, "3") == 0, "reloaded point 1 name is '3'");

    cleanup_test_job_dir();
}

static void test_rewrite_csv_empty_store(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    MeasurePointStore store;
    measure_points_init(&store);

    char csv_path[600];
    measure_points_csv_path(TEST_JOB_DIR, csv_path, sizeof(csv_path));

    ASSERT(measure_points_rewrite_csv(csv_path, &store) == GM_OK,
          "rewrite_csv with empty store succeeds");

    /* Reload -- must succeed and give an empty store (header-only
     * file, same 'empty is not an error' convention). */
    MeasurePointStore reloaded;
    ASSERT(measure_points_load_csv(csv_path, &reloaded) == GM_OK,
          "loading the rewritten empty CSV succeeds");
    ASSERT(reloaded.count == 0, "reloaded store is empty");

    cleanup_test_job_dir();
}

static void test_rewrite_csv_null_guard(void)
{
    ASSERT(measure_points_rewrite_csv("/tmp/unused.csv", NULL) == GM_ERR_GENERIC,
          "rewrite_csv with NULL store returns GM_ERR_GENERIC");
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    /* Path helpers */
    test_path_helpers();

    /* NULL guards */
    test_null_guards();

    /* Export directory creation */
    test_creates_export_dir();

    /* Empty store */
    test_empty_store_pnezd();
    test_empty_store_landxml();

    /* PNEZD column order and values */
    test_pnezd_nd_north_column_order_and_values();
    test_pnezd_no_header_row();
    test_pnezd_local_fallback_imperial();

    /* Formula injection safety */
    test_pnezd_formula_injection_safety();
    test_localization_correction_reaches_pnezd_export();
    test_localization_correction_is_zero_noop_by_default();
    test_txt_header_and_crlf();
    test_txt_data_rows_match_csv_values();
    test_txt_non_nd_north_zone_line();
    test_txt_empty_store();

    /* LandXML content */
    test_landxml_point_content();
    test_landxml_no_illegal_comment_sequence();

    /* measure_points_remove() */
    test_remove_from_middle();
    test_remove_last_element();
    test_remove_only_element();
    test_remove_out_of_range();
    test_remove_null_store();

    /* measure_points_rewrite_csv() */
    test_rewrite_csv_after_remove();
    test_rewrite_csv_empty_store();
    test_rewrite_csv_null_guard();

    if (g_tests_failed == 0) {
        printf("All %d measure_points_export tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d measure_points_export tests FAILED.\n",
                g_tests_failed, g_tests_run);
        return 1;
    }
}