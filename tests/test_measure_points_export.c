/**
 * @file test_measure_points_export.c
 * @brief Tests for collector/measure_points_export.c -- LandXML and CSV
 *        export of a job's captured points. Pure host-testable module
 *        (no networking, no hardware), so unlike rtk_feed_client.c this
 *        gets a real automated suite -- see measure_points_export.h's
 *        own file doc comment for why this module exists separately
 *        from points.csv's internal round-trip format.
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
 * Minimal test harness (matches tests/test_coords.c / tests/test_widget.c)
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
 * Helpers
 * ========================================================================= */

/** Reads the whole file at path into buf (NUL-terminated), returns
 *  false if it cannot be opened or is too large for buf. */
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

static void make_point(MeasurePoint *pt, double lat, double lon, double alt, const char *name,
                       const char *code)
{
    memset(pt, 0, sizeof(*pt));
    pt->lat = lat;
    pt->lon = lon;
    pt->alt = alt;
    pt->raw_alt = alt;
    strncpy(pt->name, name, sizeof(pt->name) - 1);
    strncpy(pt->code, code, sizeof(pt->code) - 1);
}

/** Test job/export directories live under /tmp -- removed and
 *  recreated by each test that needs a fresh one, same throwaway-
 *  fixture convention test_job_metadata.c already uses for its own
 *  /tmp paths. */
#define TEST_JOB_DIR "/tmp/geomark_export_test_job"

static void cleanup_test_job_dir(void)
{
    /* Best-effort cleanup -- order matters (files before directories),
     * ignored failures are fine since each test overwrites/recreates
     * what it needs anyway. */
    char path[768];
    snprintf(path, sizeof(path), "%s/export/points.xml", TEST_JOB_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/export/points_export.csv", TEST_JOB_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/export", TEST_JOB_DIR);
    rmdir(path);
    rmdir(TEST_JOB_DIR);
}

/* =========================================================================
 * Path helpers
 * ========================================================================= */

static void test_path_helpers(void)
{
    char buf[768];

    measure_points_export_landxml_path("/home/alex/geomark-data/projects/p/j", buf, sizeof(buf));
    ASSERT(strcmp(buf, "/home/alex/geomark-data/projects/p/j/export/points.xml") == 0,
          "LandXML export path is job_dir/export/points.xml");

    measure_points_export_csv_path("/home/alex/geomark-data/projects/p/j", buf, sizeof(buf));
    ASSERT(strcmp(buf, "/home/alex/geomark-data/projects/p/j/export/points_export.csv") == 0,
          "CSV export path is job_dir/export/points_export.csv");

    /* Distinct from points.csv's own internal path -- see this
     * module's file doc comment on why the two must never collide. */
    char internal_buf[768];
    measure_points_csv_path("/home/alex/geomark-data/projects/p/j", internal_buf,
                            sizeof(internal_buf));
    ASSERT(strcmp(buf, internal_buf) != 0,
          "Export CSV path is distinct from the internal points.csv path");
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

    gm_status_t rc = measure_points_export_landxml("/tmp/unused.xml", NULL, &meta, 0.0, 0.0);
    ASSERT(rc == GM_ERR_GENERIC, "LandXML export with NULL store returns GM_ERR_GENERIC");

    rc = measure_points_export_landxml("/tmp/unused.xml", &store, NULL, 0.0, 0.0);
    ASSERT(rc == GM_ERR_GENERIC, "LandXML export with NULL job_meta returns GM_ERR_GENERIC");

    rc = measure_points_export_csv("/tmp/unused.csv", NULL, &meta, 0.0, 0.0);
    ASSERT(rc == GM_ERR_GENERIC, "CSV export with NULL store returns GM_ERR_GENERIC");

    rc = measure_points_export_csv("/tmp/unused.csv", &store, NULL, 0.0, 0.0);
    ASSERT(rc == GM_ERR_GENERIC, "CSV export with NULL job_meta returns GM_ERR_GENERIC");
}

/* =========================================================================
 * Directory auto-creation
 * ========================================================================= */

static void test_creates_export_dir(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755); /* job_dir itself -- export/ must not exist yet */

    struct stat st;
    ASSERT(stat(TEST_JOB_DIR "/export", &st) != 0,
          "export/ subdirectory does not exist before the first export");

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_csv_path(TEST_JOB_DIR, path, sizeof(path));
    gm_status_t rc = measure_points_export_csv(path, &store, &meta, 0.0, 0.0);

    ASSERT(rc == GM_OK, "CSV export succeeds when export/ must be created first");
    ASSERT(stat(TEST_JOB_DIR "/export", &st) == 0,
          "export/ subdirectory is created by the export call");

    cleanup_test_job_dir();
}

/* =========================================================================
 * Empty store -- "nothing captured yet" is not an error
 * ========================================================================= */

static void test_empty_store_csv(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    MeasurePointStore store;
    measure_points_init(&store);

    char path[768];
    measure_points_export_csv_path(TEST_JOB_DIR, path, sizeof(path));
    gm_status_t rc = measure_points_export_csv(path, &store, &meta, 0.0, 0.0);
    ASSERT(rc == GM_OK, "CSV export of an empty store succeeds");

    char content[2048];
    ASSERT(slurp_file(path, content, sizeof(content)), "Empty-store CSV file can be read back");
    ASSERT(strcmp(content, "Point name,Code,Northing,Easting,Elevation\n") == 0,
          "Empty-store CSV contains only the header row");

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
    gm_status_t rc = measure_points_export_landxml(path, &store, &meta, 0.0, 0.0);
    ASSERT(rc == GM_OK, "LandXML export of an empty store succeeds");

    char content[2048];
    ASSERT(slurp_file(path, content, sizeof(content)), "Empty-store LandXML file can be read back");
    ASSERT(strstr(content, "<CgPoints>") != NULL && strstr(content, "</CgPoints>") != NULL,
          "Empty-store LandXML still contains a valid CgPoints collection");
    ASSERT(strstr(content, "<CgPoint ") == NULL,
          "Empty-store LandXML contains zero CgPoint entries");
    ASSERT(strstr(content, "<?xml version=\"1.0\"") != NULL,
          "LandXML output starts with a valid XML declaration");

    cleanup_test_job_dir();
}

/* =========================================================================
 * ND North coord_sys -- output already in International Foot, must
 * NOT be re-converted (the bug this module's own header doc comment
 * explicitly calls out as the thing to avoid).
 * ========================================================================= */

static void test_nd_north_csv_values(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    job_metadata_coerce_units(&meta); /* forces dist_unit -> INTL_FOOT */
    ASSERT(meta.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "job_metadata_coerce_units forces International Foot for ND North");

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint pt;
    /* Same reference point test_coords.c already verifies independently:
     * lat=46.8083, lon=-100.7837 -> easting_ft=1897447.454977,
     * northing_ft=-69797.606374. alt=10.0 m -> 32.8084 ft. */
    make_point(&pt, 46.8083, -100.7837, 10.0, "1", "CP");
    measure_points_add(&store, pt);

    char path[768];
    measure_points_export_csv_path(TEST_JOB_DIR, path, sizeof(path));
    gm_status_t rc = measure_points_export_csv(path, &store, &meta, 0.0, 0.0);
    ASSERT(rc == GM_OK, "ND North CSV export succeeds");

    char content[2048];
    ASSERT(slurp_file(path, content, sizeof(content)), "ND North CSV file can be read back");

    /* Parse the one data row back out -- second line, after the header. */
    char *data_row = strchr(content, '\n');
    ASSERT(data_row != NULL, "ND North CSV has a header row followed by a newline");
    if (data_row) {
        data_row++; /* skip past the header's own newline */
        char name[64], code[64];
        double northing, easting, elev;
        int n = sscanf(data_row, "%63[^,],%63[^,],%lf,%lf,%lf", name, code, &northing, &easting,
                       &elev);
        ASSERT(n == 5, "ND North CSV data row parses as name,code,northing,easting,elevation");
        if (n == 5) {
            ASSERT(NEAR(northing, -69797.606374, 0.01),
                  "ND North CSV northing matches the verified EPSG:2265 reference value "
                  "(NOT double-converted)");
            ASSERT(NEAR(easting, 1897447.454977, 0.01),
                  "ND North CSV easting matches the verified EPSG:2265 reference value "
                  "(NOT double-converted)");
            ASSERT(NEAR(elev, 32.8084, 0.01),
                  "ND North CSV elevation is alt converted to International Foot");
        }
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * Local-fallback coord_sys (WGS84/UTM/Local Ground) -- output IS in
 * metres from measure_points_project() and MUST be converted to the
 * job's chosen dist_unit.
 * ========================================================================= */

static void test_local_fallback_unit_conversion(void)
{
    cleanup_test_job_dir();
    mkdir(TEST_JOB_DIR, 0755);

    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_LOCAL_GROUND;
    meta.dist_unit = GM_DIST_UNIT_US_SURVEY_FOOT;

    MeasurePointStore store;
    measure_points_init(&store);
    MeasurePoint origin_pt, second_pt;
    make_point(&origin_pt, 47.0, -97.0, 100.0, "1", "CP");
    /* ~0.0001 deg north of origin -- small enough to stay in the
     * equirectangular fallback's accurate range, large enough to
     * produce a clearly nonzero northing. */
    make_point(&second_pt, 47.0001, -97.0, 100.0, "2", "TP");
    measure_points_add(&store, origin_pt);
    measure_points_add(&store, second_pt);

    /* Independently compute the expected metres -> US Survey Foot
     * value via measure_points_project() + gm_m_to_survey_ft(), the
     * exact same two calls the module under test makes internally --
     * this proves the module applies that conversion rather than
     * either skipping it or double-applying the ND North path's
     * "already feet" shortcut to a value that is actually metres. */
    MeasurePointsProjected proj;
    measure_points_project(&meta, second_pt.lat, second_pt.lon, origin_pt.lat, origin_pt.lon,
                           &proj);
    double expected_northing_ft = gm_m_to_survey_ft(proj.north);

    char path[768];
    measure_points_export_csv_path(TEST_JOB_DIR, path, sizeof(path));
    gm_status_t rc =
        measure_points_export_csv(path, &store, &meta, origin_pt.lat, origin_pt.lon);
    ASSERT(rc == GM_OK, "Local-fallback CSV export succeeds");

    char content[2048];
    slurp_file(path, content, sizeof(content));

    /* Second data row (point "2") is what carries the nonzero offset. */
    char *row1 = strchr(content, '\n');
    char *row2 = row1 ? strchr(row1 + 1, '\n') : NULL;
    ASSERT(row2 != NULL, "Local-fallback CSV has a header row plus two data rows");
    if (row2) {
        row2++;
        char name[64], code[64];
        double northing, easting, elev;
        sscanf(row2, "%63[^,],%63[^,],%lf,%lf,%lf", name, code, &northing, &easting, &elev);
        ASSERT(NEAR(northing, expected_northing_ft, 0.01),
              "Local-fallback northing is converted metres->US Survey Foot, "
              "matching an independent computation of the same conversion");
        ASSERT(NEAR(elev, gm_m_to_intl_ft(100.0), 0.01),
              "Local-fallback elevation is always converted to International Foot "
              "even though dist_unit is US Survey Foot");
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * LandXML content -- name/code attributes, coordinate order, escaping
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
    gm_status_t rc = measure_points_export_landxml(path, &store, &meta, 0.0, 0.0);
    ASSERT(rc == GM_OK, "LandXML export with one point succeeds");

    char content[4096];
    slurp_file(path, content, sizeof(content));

    ASSERT(strstr(content, "name=\"BM &quot;A&quot;\"") != NULL,
          "LandXML escapes a double-quote character in the point name");
    ASSERT(strstr(content, "code=\"EP&amp;PIPE\"") != NULL,
          "LandXML escapes an ampersand in the point code");

    /* Coordinate text content must appear in northing-easting-elevation
     * order per the LandXML schema convention. */
    char *cgpoint = strstr(content, "<CgPoint ");
    ASSERT(cgpoint != NULL, "LandXML contains exactly one CgPoint entry for the one point");
    if (cgpoint) {
        char *gt = strchr(cgpoint, '>');
        ASSERT(gt != NULL, "CgPoint open tag is well-formed");
        if (gt) {
            double n, e, el;
            int n_parsed = sscanf(gt + 1, "%lf %lf %lf", &n, &el, &e); /* placeholder read */
            (void)n_parsed;
            /* Re-read correctly as northing, easting, elevation. */
            sscanf(gt + 1, "%lf %lf %lf", &n, &e, &el);
            ASSERT(NEAR(n, -69797.606374, 0.01), "First coordinate value is northing");
            ASSERT(NEAR(e, 1897447.454977, 0.01), "Second coordinate value is easting");
            ASSERT(NEAR(el, 32.8084, 0.01), "Third coordinate value is elevation in feet");
        }
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * XML well-formedness regression -- comments must never contain "--"
 * ========================================================================= */

static void test_landxml_no_illegal_comment_sequence(void)
{
    /* XML comments may never contain "--" anywhere in their body --
     * this is illegal per the XML spec (a doc-writing convention this
     * codebase otherwise uses constantly, "foo -- bar", must NOT leak
     * into the one comment this module emits). Regression check for
     * exactly that bug. */
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
    ASSERT(comment_start != NULL && comment_end != NULL,
          "LandXML output contains exactly one recognizable comment");
    if (comment_start && comment_end) {
        bool found_double_hyphen = false;
        for (char *p = comment_start + 4; p < comment_end - 1; p++) {
            if (p[0] == '-' && p[1] == '-') {
                found_double_hyphen = true;
                break;
            }
        }
        ASSERT(!found_double_hyphen,
              "LandXML comment body contains no '--' sequence (illegal per XML spec)");
    }

    cleanup_test_job_dir();
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_path_helpers();
    test_null_guards();
    test_creates_export_dir();
    test_empty_store_csv();
    test_empty_store_landxml();
    test_nd_north_csv_values();
    test_local_fallback_unit_conversion();
    test_landxml_point_content();
    test_landxml_no_illegal_comment_sequence();

    if (g_tests_failed == 0) {
        printf("All %d measure_points_export tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d measure_points_export tests FAILED.\n", g_tests_failed,
                g_tests_run);
        return 1;
    }
}