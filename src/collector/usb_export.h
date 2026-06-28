/**
 * @file usb_export.h
 * @brief Detects the fixed-mount-point USB export drive and builds
 *        per-job paths under it, mirroring the Pi's own
 *        ~/geomark-data/projects/<project>/<job>/ structure.
 *
 * Deliberately a new, separate module rather than folding this into
 * measure_points_export.h (which only knows how to WRITE LandXML/CSV
 * given an already-resolved path string, see that header's own
 * file-level doc comment) or job_context.h (which only resolves the
 * Pi-internal job_dir, has no concept of removable media). "Where do
 * these bytes end up" (this module) and "what bytes do we write" (the
 * existing export module) are separate concerns -- export_screen.c
 * wires the two together, same separation job_create_screen.c already
 * keeps between create_job_dir() (where) and job_metadata_save()
 * (what).
 *
 * Mount point is a fixed constant (USB_EXPORT_MOUNT_POINT), not
 * detected/scanned -- this assumes the field crew's USB stick is
 * always mounted at the same path via a permanent /etc/fstab entry
 * (UUID-keyed, set up alongside this module -- see the session notes
 * for the exact fstab line), the same "one predictable physical setup"
 * assumption DEFAULT_POLE_TOP_HOST already makes for the rover's IP.
 * No udev/hotplug scanning, no support for "whichever USB stick happens
 * to be plugged in" -- if a different drive is ever plugged in instead,
 * it either isn't mounted at this path at all (usb_export_is_mounted()
 * correctly reports false) or, if someone manually mounts something
 * else here, this module has no way to know that isn't the field
 * crew's intended export drive. Out of scope for this module the same
 * way validating which physical radio is plugged in is out of scope
 * for stream/radio.c.
 *
 * Detection: usb_export_is_mounted() greps /proc/mounts for
 * USB_EXPORT_MOUNT_POINT as an exact mountpoint field -- this is the
 * standard, dependency-free way to check "is anything mounted here" on
 * Linux (no statfs()/st_dev comparison, which has its own edge cases
 * with bind mounts the simpler text check avoids). A path merely
 * existing as an empty directory (e.g. before the drive is ever
 * plugged in, or after it's unplugged with the mountpoint left behind)
 * must NOT be reported as mounted -- this module exists specifically
 * so export_screen.c can tell "the drive is actually here" apart from
 * "/mnt/usb is just a directory with nothing mounted on it", and a
 * stat()-only check cannot make that distinction.
 *
 * Directory creation: usb_export_path_for_job() creates
 * <mount>/<project>/<job>/ if missing, two levels deep (mount point
 * itself is guaranteed to exist by definition of being a mount point),
 * same bottom-up parent-first mkdir_if_missing() pattern
 * new_project_screen.c's create_project_dir() and job_create_screen.c's
 * create_job_dir() already each establish for the Pi-internal
 * equivalent of this same structure.
 *
 * project_name is recovered from job_dir's own path structure
 * (job_dir is always "<home>/geomark-data/projects/<project>/<job>",
 * see job_context.c's job_context_set()) rather than threading a
 * separate ProjectContext parameter through export_screen_init() and
 * every one of its test call sites -- job_dir already encodes the
 * project name, so re-deriving it here avoids a parameter that would
 * just duplicate information the caller already has packed into a
 * string it's passing anyway.
 */

#ifndef GEOMARK_USB_EXPORT_H
#define GEOMARK_USB_EXPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "geomark.h"

/** Fixed mount point for the field crew's export USB stick -- see this
 *  header's file-level doc comment for why this is a constant rather
 *  than detected/scanned. Set up via a permanent UUID-keyed
 *  /etc/fstab entry on geomark-handheld, not a manual `mount` command. */
#define USB_EXPORT_MOUNT_POINT "/mnt/usb"

/** Generous ceiling for a project or job name recovered from a
 *  job_dir path -- matches GM_JOB_NAME_MAX/PROJECT_CONTEXT_NAME_MAX
 *  (32 chars each), the actual on-disk name length limit every name
 *  under job_dir is already constrained to. */
#define USB_EXPORT_NAME_MAX 64

/** Matches the generous-ceiling convention of every other path buffer
 *  in this codebase (job_create_screen.c / open_job_screen.c, etc.). */
#define USB_EXPORT_PATH_MAX 768

/**
 * True if a filesystem is actually mounted at USB_EXPORT_MOUNT_POINT
 * right now (checked against /proc/mounts, not just "the directory
 * exists") -- see this header's file-level doc comment for why that
 * distinction matters. False if /proc/mounts cannot be read at all
 * (treated the same as "not mounted": callers should fall back to
 * internal storage, not treat a procfs read failure as a hard error).
 */
bool usb_export_is_mounted(void);

/**
 * Core implementation of usb_export_path_for_job() below, parameterized
 * over the mount point rather than hardcoded to
 * USB_EXPORT_MOUNT_POINT -- exists ONLY so this module's path-
 * construction/directory-creation logic can be exercised by
 * tests/test_usb_export.c against a disposable temp directory standing
 * in for the real USB mount, rather than every test needing to create
 * (and clean up) the genuine USB_EXPORT_MOUNT_POINT path on whatever
 * machine runs the suite -- doing that for real would risk leaving a
 * stray plain directory at /mnt/usb on a machine where that path is
 * supposed to mean "the actual mounted drive" (see this header's
 * file-level doc comment on why a plain directory there is exactly the
 * confusing situation usb_export_is_mounted() exists to distinguish
 * from a real mount). Production code should always call
 * usb_export_path_for_job() (the fixed-mount-point wrapper immediately
 * below), never this function directly -- mount_point is not meant to
 * vary at runtime in the real product, only in tests.
 */
gm_status_t usb_export_path_for_job_under(const char *mount_point, const char *job_dir,
                                          char *out_xml_path, size_t xml_len, char *out_csv_path,
                                          size_t csv_len);

/**
 * Derives <project>/<job> from job_dir's own path structure (see this
 * header's file-level doc comment for the assumed
 * ".../projects/<project>/<job>" shape job_context.c always produces),
 * creates <mount>/<project>/<job>/ under USB_EXPORT_MOUNT_POINT if it
 * doesn't already exist, and fills out_xml_path/out_csv_path with the
 * full paths to write LandXML/CSV export output to (same two fixed
 * filenames measure_points_export_landxml_path()/_csv_path() already
 * use for the internal job_dir/export/ location -- "points.xml" /
 * "points_export.csv" -- so the two destinations differ only in
 * directory, not filename convention).
 *
 * Does NOT check usb_export_is_mounted() itself -- callers must do
 * that first and only call this once they know the drive is actually
 * there (see export_screen.c's on_export_landxml()/on_export_csv() for
 * the calling convention this assumes). Calling this with nothing
 * mounted at USB_EXPORT_MOUNT_POINT will attempt to mkdir() directly
 * into whatever plain directory happens to be sitting at that path
 * instead, silently writing to internal storage under a path that
 * looks like the USB drive's -- exactly the confusing failure mode
 * the separate is_mounted() check exists to prevent.
 *
 * Thin wrapper over usb_export_path_for_job_under() with mount_point
 * fixed to USB_EXPORT_MOUNT_POINT -- see that function's own doc
 * comment for why the parameterized version exists at all (testing
 * only; production code always calls this fixed-mount-point version).
 *
 * Returns GM_OK on success. Returns GM_ERR_GENERIC if job_dir does not
 * have the expected ".../projects/<project>/<job>" shape (cannot
 * recover a project or job name from it) or if either name, once
 * recovered, would not fit USB_EXPORT_NAME_MAX. Returns GM_ERR_IO if
 * any directory level fails to create (permission, disk full, etc.).
 */
gm_status_t usb_export_path_for_job(const char *job_dir, char *out_xml_path, size_t xml_len,
                                    char *out_csv_path, size_t csv_len);

#endif /* GEOMARK_USB_EXPORT_H */