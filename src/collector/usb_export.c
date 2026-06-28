/**
 * @file usb_export.c
 * @brief Implementation -- see usb_export.h for design rationale.
 */

#define _GNU_SOURCE

#include "collector/usb_export.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/log.h"

/* -------------------------------------------------------------------------
 * Mount detection
 *
 * /proc/mounts format (one mount per line, whitespace-separated):
 *   <device> <mountpoint> <fstype> <options> <dump> <pass>
 * Only the second field matters here -- comparing it for an EXACT
 * match against USB_EXPORT_MOUNT_POINT, not a prefix/substring match,
 * since a substring check would wrongly match an unrelated mount at a
 * longer path that happens to start with the same characters (e.g. a
 * hypothetical /mnt/usb-backup mounted alongside this one).
 * ---------------------------------------------------------------------- */

bool usb_export_is_mounted(void)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) {
        log_warn("usb_export: cannot open /proc/mounts -- treating as not mounted");
        return false;
    }

    char line[512];
    bool found = false;

    while (fgets(line, sizeof(line), f)) {
        char device[256];
        char mountpoint[256];

        /* %255s with explicit width caps, matching this codebase's own
         * sscanf()-with-width convention (gnss/nmea.c's sentence
         * parsing uses the same bounded-width approach to avoid any
         * possibility of a stack buffer overflow from an unexpectedly
         * long /proc/mounts field). */
        if (sscanf(line, "%255s %255s", device, mountpoint) != 2)
            continue; /* malformed line -- skip, do not treat as a match */

        if (strcmp(mountpoint, USB_EXPORT_MOUNT_POINT) == 0) {
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

/* -------------------------------------------------------------------------
 * Directory creation -- same mkdir_if_missing() convention
 * new_project_screen.c's create_project_dir() and job_create_screen.c's
 * create_job_dir() already each establish.
 * ---------------------------------------------------------------------- */

static bool mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("usb_export: cannot create directory %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * project/job name recovery from job_dir
 *
 * job_dir is always "<home>/geomark-data/projects/<project>/<job>"
 * (job_context.c's job_context_set() -- see this header's file-level
 * doc comment). Recovered by walking backward from the end: job_name is
 * everything after the last '/', project_name is everything between
 * the second-to-last and last '/'. Deliberately hand-rolled rather than
 * libgen.h's dirname()/basename() -- both of those may modify their
 * input buffer in place and have platform-varying behavior on edge
 * cases (trailing slash, no slash at all), which this codebase avoids
 * the same way it avoids every other third-party/standard-library
 * parsing shortcut with surprising edge-case behavior (see e.g.
 * measure_points_screen.c's parse_purely_numeric() hand-rolling its own
 * "is this string purely numeric" check rather than relying on a
 * library function's looser definition of the same idea).
 * ---------------------------------------------------------------------- */

static bool extract_project_and_job(const char *job_dir, char *project_out, size_t project_cap,
                                    char *job_out, size_t job_cap)
{
    size_t len = strlen(job_dir);
    if (len == 0)
        return false;

    /* Find the last '/' -- job_name starts right after it. */
    const char *last_slash = strrchr(job_dir, '/');
    if (!last_slash || *(last_slash + 1) == '\0')
        return false; /* no '/' at all, or job_dir ends in '/' with nothing after */

    const char *job_name = last_slash + 1;
    size_t job_name_len = strlen(job_name);
    if (job_name_len + 1 > job_cap)
        return false; /* job name too long for caller's buffer */

    /* Find the second-to-last '/' by searching backward from
     * last_slash - 1 -- project_name is between it and last_slash. */
    if (last_slash == job_dir)
        return false; /* job_dir was just "/<job>", no project component */

    const char *search_end = last_slash - 1;
    const char *second_last_slash = NULL;
    for (const char *p = search_end; p >= job_dir; p--) {
        if (*p == '/') {
            second_last_slash = p;
            break;
        }
    }
    if (!second_last_slash)
        return false; /* no project component before the job component */

    const char *project_name     = second_last_slash + 1;
    size_t      project_name_len = (size_t)(last_slash - project_name);
    if (project_name_len == 0 || project_name_len + 1 > project_cap)
        return false; /* empty or too long for caller's buffer */

    memcpy(project_out, project_name, project_name_len);
    project_out[project_name_len] = '\0';

    memcpy(job_out, job_name, job_name_len);
    job_out[job_name_len] = '\0';

    return true;
}

gm_status_t usb_export_path_for_job_under(const char *mount_point, const char *job_dir,
                                          char *out_xml_path, size_t xml_len, char *out_csv_path,
                                          size_t csv_len)
{
    if (!mount_point || !job_dir || !out_xml_path || !out_csv_path)
        return GM_ERR_GENERIC;

    char project_name[USB_EXPORT_NAME_MAX];
    char job_name[USB_EXPORT_NAME_MAX];

    if (!extract_project_and_job(job_dir, project_name, sizeof(project_name), job_name,
                                 sizeof(job_name))) {
        log_warn("usb_export: cannot recover project/job name from job_dir '%s'", job_dir);
        return GM_ERR_GENERIC;
    }

    /* USB_EXPORT_PATH_MAX-sized, same generous-ceiling convention every
     * other path buffer in this codebase uses -- unlike a single fixed
     * USB_EXPORT_MOUNT_POINT constant, mount_point is a runtime
     * parameter here (see usb_export_path_for_job_under()'s own doc
     * comment in the header for why), so GCC cannot statically bound
     * its length the way it could when this used to be hardcoded to
     * the compile-time USB_EXPORT_MOUNT_POINT constant. Explicit
     * snprintf()-return-value truncation checks below cover what the
     * compiler can no longer prove on its own. */
    char project_dir[USB_EXPORT_PATH_MAX];
    char job_dir_on_mount[USB_EXPORT_PATH_MAX];

    int n = snprintf(project_dir, sizeof(project_dir), "%s/%s", mount_point, project_name);
    if (n < 0 || (size_t)n >= sizeof(project_dir)) {
        log_warn("usb_export: mount point + project name too long for internal buffer");
        return GM_ERR_GENERIC;
    }

    n = snprintf(job_dir_on_mount, sizeof(job_dir_on_mount), "%s/%s", project_dir, job_name);
    if (n < 0 || (size_t)n >= sizeof(job_dir_on_mount)) {
        log_warn("usb_export: mount point + project + job name too long for internal buffer");
        return GM_ERR_GENERIC;
    }

    /* Bottom-up, parent-first -- mount_point itself is guaranteed to
     * exist (it's the mount point production callers already confirmed
     * is mounted via usb_export_is_mounted(), or a pre-existing
     * disposable test directory in tests/test_usb_export.c), so only
     * these two levels need creating. */
    if (!mkdir_if_missing(project_dir))
        return GM_ERR_IO;
    if (!mkdir_if_missing(job_dir_on_mount))
        return GM_ERR_IO;

    n = snprintf(out_xml_path, xml_len, "%s/points.xml", job_dir_on_mount);
    if (n < 0 || (size_t)n >= xml_len) {
        log_warn("usb_export: resolved job directory too long for caller's xml path buffer");
        return GM_ERR_GENERIC;
    }

    n = snprintf(out_csv_path, csv_len, "%s/points_export.csv", job_dir_on_mount);
    if (n < 0 || (size_t)n >= csv_len) {
        log_warn("usb_export: resolved job directory too long for caller's csv path buffer");
        return GM_ERR_GENERIC;
    }

    return GM_OK;
}

gm_status_t usb_export_path_for_job(const char *job_dir, char *out_xml_path, size_t xml_len,
                                    char *out_csv_path, size_t csv_len)
{
    return usb_export_path_for_job_under(USB_EXPORT_MOUNT_POINT, job_dir, out_xml_path, xml_len,
                                         out_csv_path, csv_len);
}