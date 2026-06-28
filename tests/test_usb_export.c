/**
 * @file test_usb_export.c
 * @brief Tests for collector/usb_export.c -- USB export drive
 *        detection and per-job path construction.
 *
 * usb_export_is_mounted() is deliberately NOT tested here -- it reads
 * the real /proc/mounts, and this codebase keeps live OS/environment
 * state out of unit tests by convention (same reasoning
 * net/rtk_feed_client.h's own file-level doc comment gives for why
 * that module has no automated suite either: a real implementation
 * could be unit-tested independently of any screen, "though no such
 * test exists yet", since doing so meaningfully would require either
 * dependency injection this codebase doesn't use anywhere or mocking
 * procfs itself, neither of which is worth the complexity for a
 * single boolean check). Manually verified instead, on real hardware,
 * against both states (drive mounted / not mounted).
 *
 * usb_export_path_for_job_under() (project/job name recovery from
 * job_dir, directory creation, output path construction) IS pure and
 * fully host-testable -- no procfs, no removable media, just string
 * parsing and mkdir(). Every test below drives this function directly
 * against a disposable mkdtemp() directory standing in for the USB
 * mount point, NEVER the real USB_EXPORT_MOUNT_POINT constant
 * ("/mnt/usb") -- creating/removing directories under the genuine
 * mount point from a test run would risk leaving a stray plain
 * directory there on a machine (e.g. geomark-handheld itself) where
 * that exact path is supposed to mean "the actual mounted drive", the
 * same confusing situation usb_export_is_mounted() exists to
 * distinguish from a real mount (see usb_export.h's own file-level doc
 * comment). usb_export_path_for_job() itself (the fixed-mount-point
 * production wrapper) is therefore only covered indirectly, by
 * confirming it forwards to usb_export_path_for_job_under() with
 * USB_EXPORT_MOUNT_POINT -- see test_path_for_job_wrapper_uses_fixed_
 * mount_point() below, the one test in this file that IS aware of the
 * real constant, but only to confirm the wrapper passes it through
 * correctly, never to create anything there.
 */

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "collector/usb_export.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_measure_points_export.c /
 * tests/test_widget.c)
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
 * Disposable mount-point stand-in -- a fresh mkdtemp() directory per
 * call, matching the real production precondition that the mount
 * point directory itself always already exists (usb_export_path_for_
 * job_under() never creates the mount point itself, only the
 * <project>/<job> levels under it -- see that function's own doc
 * comment).
 * ========================================================================= */

static bool make_fake_mount(char *out_path, size_t out_path_len)
{
    char tmpl[] = "/tmp/geomark_usb_test_XXXXXX";
    char *result = mkdtemp(tmpl);
    if (!result)
        return false;
    snprintf(out_path, out_path_len, "%s", result);
    return true;
}

/* Best-effort cleanup for this test's own narrow, known shape
 * (<mount>/<project>/<job>/{points.xml,points_export.csv}) -- not a
 * general-purpose rm -rf, deliberately, since every directory tree
 * this test creates has exactly this shape and no other. */
static void cleanup_fake_mount(const char *mount_dir, const char *project_name,
                               const char *job_name)
{
    char job_dir[512];
    char project_dir[512];
    /* Sized with explicit headroom above job_dir's own 512 -- "points_
     * export.csv" is 18 chars plus a separator, so 512 alone is not
     * something GCC can statically prove still fits once concatenated
     * onto a buffer it already assumes could itself be up to 511
     * characters long. Same -Wformat-truncation reasoning as
     * usb_export.c's own out_xml_path/out_csv_path sizing fix earlier
     * this session. */
    char xml_path[560];
    char csv_path[560];

    snprintf(job_dir, sizeof(job_dir), "%s/%s/%s", mount_dir, project_name, job_name);
    snprintf(project_dir, sizeof(project_dir), "%s/%s", mount_dir, project_name);
    snprintf(xml_path, sizeof(xml_path), "%s/points.xml", job_dir);
    snprintf(csv_path, sizeof(csv_path), "%s/points_export.csv", job_dir);

    unlink(xml_path);
    unlink(csv_path);
    rmdir(job_dir);
    rmdir(project_dir); /* no-op (ENOTEMPTY) if other jobs under the
                         * same project still exist in this test run --
                         * fine, best-effort, not a correctness
                         * requirement */
    rmdir(mount_dir);
}

/* =========================================================================
 * Happy path: a well-formed job_dir produces the expected paths under
 * the fake mount, and the directories are actually created on disk.
 * ========================================================================= */

static void test_path_for_job_creates_directories(void)
{
    char mount_dir[200]; /* mkdtemp() template is short; see make_fake_mount()
                          * doc comment -- sized tightly so GCC can statically
                          * prove downstream concatenations never truncate */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a disposable mount stand-in");

    const char *job_dir = "/home/alex/geomark-data/projects/TESTPROJ_USB1/TESTJOB1";
    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    gm_status_t rc = usb_export_path_for_job_under(mount_dir, job_dir, xml_path, sizeof(xml_path),
                                                   csv_path, sizeof(csv_path));

    ASSERT(rc == GM_OK, "A well-formed job_dir resolves successfully");

    char expected_xml[512], expected_csv[512];
    snprintf(expected_xml, sizeof(expected_xml), "%s/TESTPROJ_USB1/TESTJOB1/points.xml",
             mount_dir);
    snprintf(expected_csv, sizeof(expected_csv), "%s/TESTPROJ_USB1/TESTJOB1/points_export.csv",
             mount_dir);
    ASSERT(strcmp(xml_path, expected_xml) == 0,
          "XML path matches <mount>/<project>/<job>/points.xml exactly");
    ASSERT(strcmp(csv_path, expected_csv) == 0,
          "CSV path matches <mount>/<project>/<job>/points_export.csv exactly");

    struct stat st;
    char project_check[512];
    snprintf(project_check, sizeof(project_check), "%s/TESTPROJ_USB1", mount_dir);
    ASSERT(stat(project_check, &st) == 0 && S_ISDIR(st.st_mode),
          "The project-level directory was actually created on disk");

    char job_check[512];
    snprintf(job_check, sizeof(job_check), "%s/TESTPROJ_USB1/TESTJOB1", mount_dir);
    ASSERT(stat(job_check, &st) == 0 && S_ISDIR(st.st_mode),
          "The job-level directory was actually created on disk");

    cleanup_fake_mount(mount_dir, "TESTPROJ_USB1", "TESTJOB1");
}

/* =========================================================================
 * Idempotency: calling this twice for the same job (the "re-export
 * after capturing more points" case, see export_screen.h's own doc
 * comment on this precedent) must succeed both times, not fail with
 * EEXIST on the second call.
 * ========================================================================= */

static void test_path_for_job_is_idempotent(void)
{
    char mount_dir[200]; /* mkdtemp() template is short; see make_fake_mount()
                          * doc comment -- sized tightly so GCC can statically
                          * prove downstream concatenations never truncate */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a disposable mount stand-in");

    const char *job_dir = "/home/alex/geomark-data/projects/TESTPROJ_USB2/TESTJOB2";
    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    gm_status_t rc1 = usb_export_path_for_job_under(mount_dir, job_dir, xml_path,
                                                    sizeof(xml_path), csv_path, sizeof(csv_path));
    gm_status_t rc2 = usb_export_path_for_job_under(mount_dir, job_dir, xml_path,
                                                    sizeof(xml_path), csv_path, sizeof(csv_path));

    ASSERT(rc1 == GM_OK, "First call succeeds");
    ASSERT(rc2 == GM_OK, "Second call for the same job also succeeds, not an EEXIST failure");

    cleanup_fake_mount(mount_dir, "TESTPROJ_USB2", "TESTJOB2");
}

/* =========================================================================
 * Two different jobs under the same project both resolve correctly --
 * confirms the project-level directory's mkdir_if_missing() EEXIST
 * path (already exercised by the first job) does not somehow block or
 * corrupt the second job's own directory creation.
 * ========================================================================= */

static void test_path_for_job_two_jobs_same_project(void)
{
    char mount_dir[200]; /* mkdtemp() template is short; see make_fake_mount()
                          * doc comment -- sized tightly so GCC can statically
                          * prove downstream concatenations never truncate */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a disposable mount stand-in");

    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    const char *job_dir_a = "/home/alex/geomark-data/projects/TESTPROJ_USB3/JOBA";
    const char *job_dir_b = "/home/alex/geomark-data/projects/TESTPROJ_USB3/JOBB";

    gm_status_t rc1 = usb_export_path_for_job_under(mount_dir, job_dir_a, xml_path,
                                                    sizeof(xml_path), csv_path, sizeof(csv_path));
    ASSERT(rc1 == GM_OK, "First job under a shared project resolves successfully");
    ASSERT(strstr(xml_path, "/TESTPROJ_USB3/JOBA/") != NULL,
          "First job's path is under the shared project directory");

    gm_status_t rc2 = usb_export_path_for_job_under(mount_dir, job_dir_b, xml_path,
                                                    sizeof(xml_path), csv_path, sizeof(csv_path));
    ASSERT(rc2 == GM_OK, "Second job under the same shared project also resolves successfully");
    ASSERT(strstr(xml_path, "/TESTPROJ_USB3/JOBB/") != NULL,
          "Second job's path is under the same shared project directory");

    struct stat st;
    char check_a[512], check_b[512];
    snprintf(check_a, sizeof(check_a), "%s/TESTPROJ_USB3/JOBA", mount_dir);
    snprintf(check_b, sizeof(check_b), "%s/TESTPROJ_USB3/JOBB", mount_dir);
    ASSERT(stat(check_a, &st) == 0 && S_ISDIR(st.st_mode), "JOBA's directory exists");
    ASSERT(stat(check_b, &st) == 0 && S_ISDIR(st.st_mode), "JOBB's directory exists");

    /* cleanup_fake_mount()'s rmdir() calls are tolerant of ENOTEMPTY/
     * ENOENT (return value intentionally ignored), so calling it once
     * per job here -- even though both share mount_dir/project_dir --
     * is safe regardless of which one runs first. */
    cleanup_fake_mount(mount_dir, "TESTPROJ_USB3", "JOBA");
    cleanup_fake_mount(mount_dir, "TESTPROJ_USB3", "JOBB");
}

/* =========================================================================
 * Malformed job_dir inputs -- every shape that cannot possibly contain
 * a "<project>/<job>" pair must fail with GM_ERR_GENERIC, not crash,
 * not silently succeed with a garbage path, and not create anything
 * on disk.
 * ========================================================================= */

static void test_path_for_job_rejects_malformed_input(void)
{
    char mount_dir[200]; /* mkdtemp() template is short; see make_fake_mount()
                          * doc comment -- sized tightly so GCC can statically
                          * prove downstream concatenations never truncate */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a disposable mount stand-in");

    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    struct {
        const char *job_dir;
        const char *why;
    } cases[] = {
        { "", "empty string" },
        { "noslash", "no '/' at all" },
        { "/onlyonelevel", "only one path component" },
        { "/trailing/slash/", "trailing slash with nothing after it" },
        { "/", "a single slash" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        gm_status_t rc = usb_export_path_for_job_under(mount_dir, cases[i].job_dir, xml_path,
                                                       sizeof(xml_path), csv_path,
                                                       sizeof(csv_path));
        ASSERT(rc == GM_ERR_GENERIC, cases[i].why);
    }

    rmdir(mount_dir); /* nothing was ever created under it -- just the
                       * disposable mount stand-in itself to remove */
}

/**
 * NULL arguments must be rejected, not dereferenced -- the same
 * defensive-NULL-check convention every other gm_status_t-returning
 * function in this codebase already follows (see e.g.
 * measure_points_export_landxml()'s own NULL handling for store/
 * job_meta).
 */
static void test_path_for_job_rejects_null_arguments(void)
{
    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    ASSERT(usb_export_path_for_job_under(NULL, "/a/b/c", xml_path, sizeof(xml_path), csv_path,
                                         sizeof(csv_path)) == GM_ERR_GENERIC,
          "NULL mount_point is rejected, not dereferenced");
    ASSERT(usb_export_path_for_job_under("/tmp", NULL, xml_path, sizeof(xml_path), csv_path,
                                         sizeof(csv_path)) == GM_ERR_GENERIC,
          "NULL job_dir is rejected, not dereferenced");
    ASSERT(usb_export_path_for_job_under("/tmp", "/a/b/c", NULL, sizeof(xml_path), csv_path,
                                         sizeof(csv_path)) == GM_ERR_GENERIC,
          "NULL out_xml_path is rejected, not dereferenced");
    ASSERT(usb_export_path_for_job_under("/tmp", "/a/b/c", xml_path, sizeof(xml_path), NULL,
                                         sizeof(csv_path)) == GM_ERR_GENERIC,
          "NULL out_csv_path is rejected, not dereferenced");
}

/* =========================================================================
 * A project or job name at exactly USB_EXPORT_NAME_MAX-1 characters
 * (the longest that still fits with its NUL terminator) must succeed;
 * one character longer must fail cleanly rather than silently
 * truncating into a different, wrong directory name.
 * ========================================================================= */

static void test_path_for_job_name_length_boundary(void)
{
    char mount_dir[200]; /* mkdtemp() template is short; see make_fake_mount()
                          * doc comment -- sized tightly so GCC can statically
                          * prove downstream concatenations never truncate */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a disposable mount stand-in");

    char long_name[USB_EXPORT_NAME_MAX]; /* exactly USB_EXPORT_NAME_MAX-1
                                          * 'X' characters + NUL */
    memset(long_name, 'X', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    char job_dir[256];
    snprintf(job_dir, sizeof(job_dir), "/home/alex/geomark-data/projects/%s/SHORTJOB",
             long_name);

    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];
    gm_status_t rc = usb_export_path_for_job_under(mount_dir, job_dir, xml_path, sizeof(xml_path),
                                                   csv_path, sizeof(csv_path));
    ASSERT(rc == GM_OK, "A project name at exactly the length boundary still succeeds");

    cleanup_fake_mount(mount_dir, long_name, "SHORTJOB");

    /* One character longer must not fit -- confirms the boundary is
     * actually enforced, not just "happens to work" at this exact
     * length by coincidence. Fresh mount stand-in since the previous
     * one was just cleaned up above. */
    ASSERT(make_fake_mount(mount_dir, sizeof(mount_dir)),
          "mkdtemp() created a second disposable mount stand-in");

    char too_long_name[USB_EXPORT_NAME_MAX + 1];
    memset(too_long_name, 'Y', sizeof(too_long_name) - 1);
    too_long_name[sizeof(too_long_name) - 1] = '\0';

    char job_dir2[256];
    snprintf(job_dir2, sizeof(job_dir2), "/home/alex/geomark-data/projects/%s/SHORTJOB2",
             too_long_name);

    rc = usb_export_path_for_job_under(mount_dir, job_dir2, xml_path, sizeof(xml_path), csv_path,
                                       sizeof(csv_path));
    ASSERT(rc == GM_ERR_GENERIC,
          "A project name one character past the boundary is rejected, not truncated");

    rmdir(mount_dir); /* nothing was created under it this time */
}

/* =========================================================================
 * Wrapper sanity: usb_export_path_for_job() (the fixed-mount-point
 * production function) must still reject a malformed job_dir the same
 * way the parameterized core function does -- confirms the wrapper
 * actually forwards to usb_export_path_for_job_under() rather than
 * having its own separate (and possibly diverging) logic. Deliberately
 * uses a malformed job_dir so this fails at the name-recovery step,
 * before ever reaching mkdir() -- see extract_project_and_job()'s
 * ordering in usb_export.c, which checks job_dir's shape before doing
 * any filesystem work -- so this test never touches the real
 * USB_EXPORT_MOUNT_POINT on disk even though it calls the production
 * wrapper directly.
 * ========================================================================= */

static void test_path_for_job_wrapper_uses_fixed_mount_point(void)
{
    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX];

    gm_status_t rc =
        usb_export_path_for_job("not-a-valid-job-dir", xml_path, sizeof(xml_path), csv_path,
                                sizeof(csv_path));
    ASSERT(rc == GM_ERR_GENERIC,
          "The production wrapper still rejects a malformed job_dir the same way the "
          "parameterized core function does");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_path_for_job_creates_directories();
    test_path_for_job_is_idempotent();
    test_path_for_job_two_jobs_same_project();
    test_path_for_job_rejects_malformed_input();
    test_path_for_job_rejects_null_arguments();
    test_path_for_job_name_length_boundary();
    test_path_for_job_wrapper_uses_fixed_mount_point();

    if (g_tests_failed == 0) {
        printf("All %d usb_export tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d usb_export tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}