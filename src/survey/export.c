/**
 * @file export.c
 * @brief CSV export and SCP transfer implementation.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "survey/export.h"
#include "util/log.h"
#include "util/units.h"

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Convert gm_fix_type_t to a CSV display string.
 * Uses the canonical names from geomark.h.
 */
static const char *fix_type_str(uint8_t fix_quality)
{
    switch ((gm_fix_type_t)fix_quality) {
        case FIX_RTK_FIXED: return "RTK_FIXED";
        case FIX_RTK_FLOAT: return "RTK_FLOAT";
        case FIX_DGPS:      return "DGPS";
        case FIX_SINGLE:    return "GPS";
        case FIX_NONE:      /* fall-through */
        default:            return "INVALID";
    }
}

/**
 * Create a directory if it does not already exist (single level — parent
 * must exist).  Returns 0 on success or if the directory already exists,
 * -1 on any other error.
 */
static int mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("export: cannot create directory %s: %s",
                 path, strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Parse /proc/mounts looking for a vfat or exfat filesystem mounted under
 * /media.  Writes the mount point into @p mount_point on success.
 * Returns true if found, false otherwise.
 */
static bool find_usb_mount(char *mount_point, size_t len)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f)
        return false;

    char line[512];
    bool found = false;

    while (fgets(line, sizeof(line), f)) {
        char dev[128], mnt[256], fs[64], opts[256];
        int  freq, passno;

        if (sscanf(line, "%127s %255s %63s %255s %d %d",
                   dev, mnt, fs, opts, &freq, &passno) < 3)
            continue;

        /* Accept vfat or exfat mounted anywhere under /media — that is
         * where udisks2 auto-mounts USB sticks on Raspberry Pi OS. */
        if ((strcmp(fs, "vfat") == 0 || strcmp(fs, "exfat") == 0) &&
            strncmp(mnt, "/media", 6) == 0) {
            snprintf(mount_point, len, "%s", mnt);
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int export_write_header(FILE *f)
{
    int rc = fprintf(f,
        "LAT,LON,ALT,POINT_CODE,DESCRIPTION,"
        "FIX_TYPE,HDOP,NUM_SATS,TIMESTAMP\n");
    if (rc < 0)
        return -1;

    if (fflush(f) != 0)
        return -1;

    return 0;
}

int export_write_point(FILE *f, const SurveyPoint *point)
{
    /* Convert altitude: metres → international feet (vertical elevations
     * use international feet per the project units spec in units.h). */
    double alt_ft = gm_m_to_intl_ft(point->alt);

    /* Format ISO-8601 UTC timestamp. */
    char ts[32];
    struct tm *utc = gmtime(&point->timestamp);
    if (!utc) {
        log_warn("export: gmtime failed for point %u", point->point_num);
        strncpy(ts, "1970-01-01T00:00:00Z", sizeof(ts) - 1);
        ts[sizeof(ts) - 1] = '\0';
    } else {
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
    }

    /* Escape description: if it contains a comma or double-quote, wrap in
     * double quotes and escape any embedded quotes as "". */
    char desc_buf[SURVEY_DESC_MAX + 4];
    if (strchr(point->desc, ',') || strchr(point->desc, '"')) {
        /* RFC 4180 CSV quoting: replace " with "" then wrap in quotes */
        size_t di = 0;
        desc_buf[di++] = '"';
        for (const char *s = point->desc; *s && di < sizeof(desc_buf) - 3; s++) {
            if (*s == '"')
                desc_buf[di++] = '"'; /* doubled quote */
            desc_buf[di++] = *s;
        }
        desc_buf[di++] = '"';
        desc_buf[di]   = '\0';
    } else {
        strncpy(desc_buf, point->desc, sizeof(desc_buf) - 1);
        desc_buf[sizeof(desc_buf) - 1] = '\0';
    }

    int rc = fprintf(f,
        "%.9f,%.9f,%.3f,%s,%s,%s,%.2f,%u,%s\n",
        point->lat,
        point->lon,
        alt_ft,
        point->code,
        desc_buf,
        fix_type_str(point->fix_quality),
        point->hdop,
        (unsigned)point->num_sats,
        ts);

    if (rc < 0)
        return -1;

    /* Flush every row — power-safe, ensures every accepted point hits disk
     * before the next capture begins. */
    if (fflush(f) != 0)
        return -1;

    return 0;
}

int export_scp(const char *csv_path, int timeout_s)
{
    /* Build "user@host:dest" — no shell expansion, constructed in C. */
    char dest[512];
    snprintf(dest, sizeof(dest), "%s@%s:%s",
             EXPORT_SCP_USER, EXPORT_SCP_HOST, EXPORT_SCP_DEST);

    log_info("export: scp %s → %s", csv_path, dest);

    pid_t pid = fork();
    if (pid < 0) {
        log_error("export: fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process — exec scp directly (no shell). */
        char *argv[] = {
            "scp",
            "-o", "StrictHostKeyChecking=no",
            "-o", "ConnectTimeout=10",
            "-o", "BatchMode=yes",        /* fail immediately if key auth
                                             is not set up — no password
                                             prompt that blocks the UI */
            (char *)csv_path,
            dest,
            NULL
        };
        execvp("scp", argv);
        /* execvp only returns on error — exit with a distinctive code. */
        _exit(127);
    }

    /* Parent — poll with WNOHANG until child exits or timeout elapses. */
    int        status;
    int        elapsed_ms = 0;
    const int  poll_ms    = 250;

    while (elapsed_ms < timeout_s * 1000) {
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                log_info("export: scp succeeded");
                return 0;
            }
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            log_warn("export: scp exited with status %d", code);
            return 1;
        }
        struct timespec ts = { 0, (long)poll_ms * 1000000L };
        nanosleep(&ts, NULL);
        elapsed_ms += poll_ms;
    }

    /* Timeout — kill child and reap to avoid a zombie. */
    log_warn("export: scp timed out after %d s — killing", timeout_s);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 2;
}

bool export_resolve_output_dir(char *out, size_t out_len)
{
    char mount[256];

    if (find_usb_mount(mount, sizeof(mount))) {
        char geomark_dir[320];
        snprintf(geomark_dir, sizeof(geomark_dir), "%s/geomark", mount);

        if (mkdir_if_missing(geomark_dir) == 0) {
            strncpy(out, geomark_dir, out_len - 1);
            out[out_len - 1] = '\0';
            log_info("export: output directory: %s (USB)", out);
            return true;
        }
        /* mkdir failed — fall through to SD card */
        log_warn("export: USB found at %s but cannot create geomark/ dir — "
                 "falling back to SD", mount);
    }

    /* SD card fallback — use $HOME/geomark/ */
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        home = "/home/alex";

    snprintf(out, out_len, "%s/geomark", home);
    mkdir_if_missing(out);
    log_info("export: output directory: %s (SD fallback)", out);
    return false;
}