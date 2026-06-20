#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "collector/job_metadata.h"

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

static void test_defaults(void)
{
    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);

    ASSERT(strcmp(meta.template_name, "Default") == 0, "Default template is 'Default'");
    ASSERT(meta.coord_sys == GM_COORD_SYS_WGS84, "Default coordinate system is WGS84");
    ASSERT(meta.dist_unit == GM_DIST_UNIT_US_SURVEY_FOOT, "Default unit is US Survey Foot");
    ASSERT(meta.cogo == GM_COGO_GROUND, "Default cogo is Ground");
    ASSERT(meta.job_name[0] == '\0', "Default job_name is empty");
    ASSERT(meta.reference[0] == '\0', "Default reference is empty");
}

static void test_coerce_units_nd_north_forces_intl_foot(void)
{
    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    meta.dist_unit = GM_DIST_UNIT_US_SURVEY_FOOT; /* deliberately wrong */

    job_metadata_coerce_units(&meta);

    ASSERT(meta.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "coerce_units forces International Foot for an ND North job");
}

static void test_coerce_units_leaves_other_systems_alone(void)
{
    gm_coord_sys_t systems[] = { GM_COORD_SYS_WGS84, GM_COORD_SYS_UTM, GM_COORD_SYS_LOCAL_GROUND };

    for (size_t i = 0; i < sizeof(systems) / sizeof(systems[0]); i++) {
        gm_job_metadata_t meta;
        job_metadata_defaults(&meta);
        meta.coord_sys = systems[i];
        meta.dist_unit = GM_DIST_UNIT_INTL_FOOT; /* an explicit, deliberate choice */

        job_metadata_coerce_units(&meta);

        ASSERT(meta.dist_unit == GM_DIST_UNIT_INTL_FOOT,
              "coerce_units leaves a non-ND-North job's unit choice untouched");
    }
}

static void test_save_load_round_trip(void)
{
    gm_job_metadata_t meta;
    job_metadata_defaults(&meta);
    strncpy(meta.job_name, "TESTJOB", sizeof(meta.job_name) - 1);
    strncpy(meta.reference, "MON-1234", sizeof(meta.reference) - 1);
    strncpy(meta.description, "Test job desc", sizeof(meta.description) - 1);
    strncpy(meta.operator_name, "Alex G", sizeof(meta.operator_name) - 1);
    strncpy(meta.notes, "Sample notes here", sizeof(meta.notes) - 1);
    meta.coord_sys = GM_COORD_SYS_ND_NORTH;
    meta.dist_unit = GM_DIST_UNIT_US_SURVEY_FOOT; /* deliberately wrong; save() must coerce it */
    meta.cogo      = GM_COGO_GRID;

    gm_status_t rc = job_metadata_save("/tmp/geomark_test_job_metadata.ini", &meta);
    ASSERT(rc == GM_OK, "save() succeeds");

    gm_job_metadata_t loaded;
    rc = job_metadata_load("/tmp/geomark_test_job_metadata.ini", &loaded);
    ASSERT(rc == GM_OK, "load() succeeds");

    ASSERT(strcmp(loaded.job_name, "TESTJOB") == 0, "job_name round-trips");
    ASSERT(strcmp(loaded.template_name, "Default") == 0, "template_name round-trips");
    ASSERT(loaded.coord_sys == GM_COORD_SYS_ND_NORTH, "coord_sys round-trips");
    ASSERT(loaded.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "dist_unit was coerced to International Foot by save(), confirmed on load");
    ASSERT(loaded.cogo == GM_COGO_GRID, "cogo round-trips");
    ASSERT(strcmp(loaded.reference, "MON-1234") == 0, "reference round-trips");
    ASSERT(strcmp(loaded.description, "Test job desc") == 0, "description round-trips");
    ASSERT(strcmp(loaded.operator_name, "Alex G") == 0, "operator round-trips");
    ASSERT(strcmp(loaded.notes, "Sample notes here") == 0, "notes round-trips");

    remove("/tmp/geomark_test_job_metadata.ini");
}

static void test_missing_file_returns_defaults(void)
{
    gm_job_metadata_t meta;
    gm_status_t rc = job_metadata_load("/tmp/geomark_nonexistent_job.ini", &meta);

    ASSERT(rc == GM_OK, "Missing file returns GM_OK, not an error");
    ASSERT(meta.coord_sys == GM_COORD_SYS_WGS84, "Missing file falls back to defaults");
}

static void test_malformed_and_unknown_lines_tolerated(void)
{
    const char *path = "/tmp/geomark_test_job_malformed.ini";
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "Can create the test fixture file");
    if (f) {
        fprintf(f, "# a comment\n");
        fprintf(f, "\n");
        fprintf(f, "this line has no equals sign\n");
        fprintf(f, "unknown_key=some_value\n");
        fprintf(f, "job_name=Survives\n");
        fclose(f);
    }

    gm_job_metadata_t meta;
    gm_status_t rc = job_metadata_load(path, &meta);

    ASSERT(rc == GM_OK, "A file with malformed/unknown lines still loads successfully");
    ASSERT(strcmp(meta.job_name, "Survives") == 0,
          "A valid key after malformed/unknown lines is still parsed correctly");

    remove(path);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_defaults();
    test_coerce_units_nd_north_forces_intl_foot();
    test_coerce_units_leaves_other_systems_alone();
    test_save_load_round_trip();
    test_missing_file_returns_defaults();
    test_malformed_and_unknown_lines_tolerated();

    if (g_tests_failed == 0) {
        printf("All %d job metadata tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d job metadata tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}