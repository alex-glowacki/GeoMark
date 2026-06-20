/**
 * @file job_metadata.h
 * @brief Job-level metadata: coordinate system, units, cogo setting, and
 *        free-text fields collected on the Job Setup screen, persisted
 *        as a small INI-style file at
 *        ~/geomark-data/projects/<project>/<job>/job.ini.
 *
 * Deliberately a separate, narrow module rather than a generalization of
 * util/config.c's config_load() (which is hardwired to gm_config_t's
 * device/baud/log/data_dir schema, with no generic key=value capability)
 * -- mirrors config_load()'s own line-scanning pattern (strip
 * comments/blanks, split on '=', match known keys) against a different,
 * job-specific schema, rather than building a shared generic parser
 * against only one real second use case. Revisit if a third metadata
 * consumer with messier needs (sections, arrays, escaping) ever shows up
 * -- at that point there's enough real evidence to generalize correctly.
 */

#ifndef GEOMARK_JOB_METADATA_H
#define GEOMARK_JOB_METADATA_H

#include "geomark.h"

/* -------------------------------------------------------------------------
 * Coordinate system library (geomark-ui-redesign-decisions.md, data model
 * section): WGS84 geographic, UTM (auto zone), Local/Site Ground, plus
 * NAD83(1986) North Dakota State Plane North Zone (EPSG:2265) -- the only
 * State Plane zone GeoMark supports; NAD83 State Plane for any other
 * state/zone remains out of scope. coords_wgs84_to_utm()/_to_ecef() are
 * still stubs (collector/coords.h); GM_COORD_SYS_UTM is selectable here
 * but has no working transform behind it yet.
 * ---------------------------------------------------------------------- */

typedef enum {
    GM_COORD_SYS_WGS84 = 0,
    GM_COORD_SYS_UTM,
    GM_COORD_SYS_LOCAL_GROUND,
    GM_COORD_SYS_ND_NORTH, /* NAD83(1986) ND State Plane North, EPSG:2265 */
} gm_coord_sys_t;

/**
 * Which foot definition a job's exported/displayed distances use.
 *
 * Not metric-vs-imperial (GeoMark is imperial-only by hard requirement,
 * see units.h) -- this chooses between the two real, legally distinct
 * foot definitions units.h already implements. The two are NOT
 * interchangeable: GM_COORD_SYS_ND_NORTH's own EPSG:2265 definition
 * fixes its unit to the international foot (verified against the
 * zone's own WKT, UNIT["foot",0.3048,...]) -- selecting US Survey Foot
 * for an ND North job would silently produce coordinates that don't
 * match the zone's actual legal grid. job_metadata_coerce_units() below
 * enforces this; the Job Setup screen also locks the dropdown in the UI
 * so the mismatch is never reachable in the first place, but the
 * enforcement lives here too since this module has no visibility into
 * whether a caller went through the screen or constructed a
 * gm_job_metadata_t some other way.
 */
typedef enum {
    GM_DIST_UNIT_US_SURVEY_FOOT = 0,
    GM_DIST_UNIT_INTL_FOOT,
} gm_dist_unit_t;

typedef enum {
    GM_COGO_GROUND = 0,
    GM_COGO_GRID,
} gm_cogo_t;

#define GM_JOB_NAME_MAX 32
#define GM_JOB_TEMPLATE_MAX 32
#define GM_JOB_REFERENCE_MAX 64
#define GM_JOB_DESC_MAX 128
#define GM_JOB_OPERATOR_MAX 64
#define GM_JOB_NOTES_MAX 256

typedef struct {
    char job_name[GM_JOB_NAME_MAX];
    char template_name[GM_JOB_TEMPLATE_MAX];
    gm_coord_sys_t coord_sys;
    gm_dist_unit_t dist_unit;
    gm_cogo_t cogo;
    char reference[GM_JOB_REFERENCE_MAX];
    char description[GM_JOB_DESC_MAX];
    char operator_name[GM_JOB_OPERATOR_MAX]; /* "operator" avoided: C keyword-adjacent / reads oddly
                                                as a member name */
    char notes[GM_JOB_NOTES_MAX];
} gm_job_metadata_t;

/** Fills defaults: template_name="Default", coord_sys=WGS84,
 *  dist_unit=US Survey Foot, cogo=Ground, all strings empty. */
void job_metadata_defaults(gm_job_metadata_t *out);

/**
 * If coord_sys is GM_COORD_SYS_ND_NORTH, forces dist_unit to
 * GM_DIST_UNIT_INTL_FOOT regardless of its current value -- the
 * enforcement described in gm_dist_unit_t's doc comment above. No-op for
 * every other coordinate system (their unit choice is a free pick, not
 * fixed by an external legal definition).
 */
void job_metadata_coerce_units(gm_job_metadata_t *meta);

/**
 * Writes meta to path as an INI-style key=value file (one level, no
 * sections -- matches util/config.c's existing on-disk convention).
 * Calls job_metadata_coerce_units() on a local copy before writing, so a
 * caller can never persist an ND North job with the wrong foot unit
 * even if it reached this function with one.
 * Returns GM_OK on success, GM_ERR_IO if the file cannot be written.
 */
gm_status_t job_metadata_save(const char *path, const gm_job_metadata_t *meta);

/**
 * Reads path into out. If the file does not exist, fills out with
 * job_metadata_defaults() and returns GM_OK (same "missing file is not
 * an error" convention config_load() already uses) -- callers that need
 * to distinguish "loaded" from "defaulted" should stat() the path
 * themselves first.
 * Returns GM_ERR_PARSE if the file exists but is malformed beyond what
 * line-by-line recovery (skip and warn) can handle, GM_OK otherwise.
 */
gm_status_t job_metadata_load(const char *path, gm_job_metadata_t *out);

#endif /* GEOMARK_JOB_METADATA_H */