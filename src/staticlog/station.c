/**
 * @file staticlog/station.c
 * @brief Implementation -- see station.h for the full workflow this
 *        feeds into and why RINEX conversion/OPUS submission are
 *        deliberately outside this codebase's scope.
 */

#define _GNU_SOURCE

#include "staticlog/station.h"
#include "util/config.h"
#include "util/log.h"
#include "gnss/um980.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * Signal handling -- same pattern base/station.c and rover/station.c
 * already each establish.
 * ----------------------------------------------------------------------- */

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/** How often to log a progress line while otherwise silently copying
 *  bytes -- frequent enough to confirm liveness within a reasonable
 *  wait over SSH, infrequent enough not to flood the log across a
 *  multi-hour occupation. */
#define PROGRESS_INTERVAL_SEC 30

gm_status_t staticlog_station_run(const char *config_path, const char *out_path)
{
    if (!out_path || out_path[0] == '\0') {
        log_error("staticlog: out_path is required");
        return GM_ERR_GENERIC;
    }

    /* --- Config --------------------------------------------------------- */
    gm_config_t cfg;
    if (config_load(config_path, &cfg) != GM_OK) {
        log_warn("staticlog: config_load failed, using defaults");
        config_defaults(&cfg);
    }

    if (cfg.log_file[0] != '\0')
        log_init(cfg.log_file);

    log_info("staticlog: serial=%s baud=%d -> %s",
             cfg.serial_device, cfg.serial_baud, out_path);

    /* --- UM980 ------------------------------------------------------------ */
    Um980 um980;
    memset(&um980, 0, sizeof(um980));

    SerialResult sr = um980_open(&um980, cfg.serial_device);
    if (sr != SERIAL_OK) {
        log_error("staticlog: um980_open(%s) failed (%d)", cfg.serial_device, sr);
        return GM_ERR_IO;
    }
    log_info("staticlog: UM980 opened on %s", cfg.serial_device);

    sr = um980_init_static_log(&um980);
    if (sr != SERIAL_OK) {
        /* um980_init_static_log() already logged exactly which command
         * was rejected -- see that function's own doc comment
         * (gnss/um980.h) on why this is the most likely failure mode
         * and how to recover from it (a short test run first). */
        log_error("staticlog: um980_init_static_log failed (%d)", sr);
        um980_close(&um980);
        return GM_ERR_GENERIC;
    }
    log_info("staticlog: UM980 configured for raw observation logging");

    /* --- Output file ------------------------------------------------------ */
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        log_error("staticlog: cannot open '%s' for writing: %s",
                 out_path, strerror(errno));
        um980_close(&um980);
        return GM_ERR_IO;
    }

    /* --- Signal handling ---------------------------------------------- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* --- Copy loop: raw bytes, UM980 -> file, no interpretation --------
     * GeoMark does not parse or understand this data -- it is opaque
     * binary destined for Unicore's own Converter tool (see station.h's
     * file-level doc comment). serial_read() already blocks up to the
     * UM980's configured timeout via select(), so this loop costs
     * nothing extra while genuinely idle between observation epochs. */
    uint8_t buf[GM_SERIAL_BUF_SIZE];
    time_t start = time(NULL);
    time_t last_progress = start;
    long long total_bytes = 0;

    log_info("staticlog: logging started -- Ctrl-C (or SIGTERM) to stop. "
             "Longer occupations produce better OPUS solutions.");

    while (g_running) {
        int n = serial_read(&um980.serial, buf, sizeof(buf));
        if (n > 0) {
            size_t written = fwrite(buf, 1, (size_t)n, out);
            if (written != (size_t)n) {
                log_error("staticlog: short write to '%s' (%zu/%d bytes) -- "
                         "check available disk space", out_path, written, n);
                break;
            }
            total_bytes += n;
        } else if (n != SERIAL_ERR_TIMEOUT) {
            log_error("staticlog: serial_read failed (%d)", n);
            break;
        }
        /* SERIAL_ERR_TIMEOUT is the normal "no data this instant" case
         * -- just loop again and let g_running be checked. */

        time_t now = time(NULL);
        if (now - last_progress >= PROGRESS_INTERVAL_SEC) {
            log_info("staticlog: %lld sec elapsed, %lld bytes written",
                     (long long)(now - start), total_bytes);
            fflush(out); /* so `tail -f`/a concurrent read sees progress,
                         * and a mid-session power loss loses at most
                         * PROGRESS_INTERVAL_SEC worth of buffered data */
            last_progress = now;
        }
    }

    time_t end = time(NULL);
    log_info("staticlog: stopped after %lld sec, %lld bytes written to '%s'",
             (long long)(end - start), total_bytes, out_path);

    /* --- Cleanup (reverse init order) ----------------------------------- */
    fclose(out);
    um980_close(&um980);

    log_info("staticlog: shutdown complete");
    return GM_OK;
}