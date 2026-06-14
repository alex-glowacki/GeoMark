/**
 * @file export.h
 * @brief CSV export and SCP transfer for survey sessions.
 *
 * CSV column order (per spec):
 *   LAT,LON,ALT,POINT_CODE,DESCRIPTION,FIX_TYPE,HDOP,NUM_SATS,TIMESTAMP
 *
 * ALT is written in international feet (converted from metres).
 * LAT/LON are written as decimal degrees to 9 decimal places.
 * TIMESTAMP is ISO-8601 UTC: "2026-06-14T14:30:22Z"
 *
 * SCP transfer is dispatched via fork()/execv() — no shell injection risk,
 * no runtime dependencies.  SSH key auth must be configured on the Pi 5
 * before SCP will succeed without a password prompt.
 */

#ifndef GEOMARK_EXPORT_H
#define GEOMARK_EXPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "survey/survey.h"

/* --------------------------------------------------------------------------
 * SCP target configuration
 * -------------------------------------------------------------------------- */

#define EXPORT_SCP_USER "alexg"
#define EXPORT_SCP_HOST "10.0.0.174"
#define EXPORT_SCP_DEST "C:/Users/alexg/Downloads/"

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * Write the CSV header row to @p f.
 * Called once by survey_session_open() — not normally called directly.
 *
 * @param f     Open writable FILE*.
 * @return  0 on success, -1 on write error.
 */
int export_write_header(FILE *f);

/**
 * Write one survey point as a CSV row to @p f and flush immediately.
 * Flushing on every row ensures no data is lost if power is cut mid-session.
 *
 * @param f      Open writable FILE*.
 * @param point  Completed survey point.
 * @return  0 on success, -1 on write error.
 */
int export_write_point(FILE *f, const SurveyPoint *point);

/**
 * Transfer @p csv_path to the Windows PC via SCP.
 * Spawns scp as a child process using fork()/execv().
 * Blocks until the transfer completes or @p timeout_s seconds elapse.
 *
 * @param csv_path   Full local path to the CSV file.
 * @param timeout_s  Seconds to wait before giving up (recommended: 30).
 * @return  0 on success, -1 on fork/exec error, 1 if scp exited non-zero,
 *          2 if timeout elapsed.
 */
int export_scp(const char *csv_path, int timeout_s);

/**
 * Resolve the USB mount path for CSV output.
 * Checks /proc/mounts for a vfat or exfat filesystem mounted under /media.
 * Creates /media/usb/geomark/ if the USB stick is found and the directory
 * does not yet exist.
 *
 * @param out     Buffer to receive the resolved directory path.
 * @param out_len Size of @p out buffer.
 * @return  true if a USB stick was found and @p out was populated,
 *          false if falling back to SD card (~/ geomark/).
 */
bool export_resolve_output_dir(char *out, size_t out_len);

#endif /* GEOMARK_EXPORT_H */