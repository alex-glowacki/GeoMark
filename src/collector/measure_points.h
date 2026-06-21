/**
 * @file measure_points.h
 * @brief Captured-point storage, CSV persistence, and map-panel
 *        projection for the Measure Points screen.
 *
 * Deliberately a new, separate module rather than reuse of either
 * existing point-shaped type in this codebase:
 *
 *   - collector/points.h (gm_point_store_t / gm_point_t): every function
 *     in points.c is an unimplemented "TODO: Phase 3" stub that always
 *     returns GM_ERR_GENERIC. There is no working behavior to inherit --
 *     adopting it would mean implementing it for the first time anyway,
 *     under a speculative assumption that some other future consumer
 *     also wants exactly this shape. No second real consumer exists
 *     today.
 *   - survey/survey.h (SurveyPoint / SurveySession): this is the real,
 *     working model, but it belongs to the legacy ui/client.c capture
 *     flow that must remain completely untouched (project hard rule).
 *     Its struct is also shaped around that flow's specific decisions
 *     (CSV-file-handle-coupled session lifecycle, baked-in 3-fix capture
 *     averaging) that were never decided for Measure Points. Coupling
 *     to it would mean a future change to the legacy capture flow could
 *     silently change Measure Points' behavior, and vice versa.
 *
 * This mirrors job_metadata.h's own precedent for the same situation:
 * a small, narrow module with its own schema, rather than generalizing
 * an existing one against only one real use case (see that header's
 * doc comment). The field set itself (lat/lon/alt/fix_quality/hdop/
 * num_sats) is intentionally the same shape gm_position_t,
 * RoverStatusPacket, and SurveyPoint all already converge on -- that
 * convergence reflects what a GNSS fix actually is, not a reason to
 * share the surrounding struct or persistence code.
 *
 * code[] is free-form text, not yet interpreted. A future point-code
 * convention (symbol prefix/suffix marking a point as a spot shot vs.
 * the start/end of a breakline, with connected-line rendering on the
 * map for breaklines) is intentionally NOT built here -- nothing in
 * this struct blocks adding that interpretation later, but it is not
 * speculative work done now.
 */

#ifndef GEOMARK_MEASURE_POINTS_H
#define GEOMARK_MEASURE_POINTS_H

#include <stdint.h>
#include <time.h>

#include "collector/job_metadata.h"
#include "geomark.h"

/* -------------------------------------------------------------------------
 * Limits
 * ---------------------------------------------------------------------- */

/** Max length of a point code string, including NUL. Matches
 *  SURVEY_CODE_MAX (survey/survey.h) -- not shared by #include, since
 *  these are independent modules (see file doc comment), but there is
 *  no reason for the two limits to diverge by accident. */
#define GM_MEASURE_POINT_CODE_MAX 32

/** Max points held in memory / round-tripped through points.csv per job.
 *  Matches SURVEY_POINTS_MAX's order of magnitude; revisit if a real job
 *  ever approaches this, same "raise when real data demands it, not on
 *  estimate" history UI_GRID_MAX_WIDGETS already has. */
#define GM_MEASURE_POINTS_MAX 4096

/* -------------------------------------------------------------------------
 * A single captured point
 * ---------------------------------------------------------------------- */

typedef struct {
    double lat;          /* decimal degrees, WGS84, +N */
    double lon;          /* decimal degrees, WGS84, +E */
    double alt;          /* meters above MSL -- internal storage unit, see units.h */
    uint8_t fix_quality; /* gm_fix_type_t value at capture time */
    double hdop;
    uint8_t num_sats;
    time_t timestamp;                     /* UTC epoch seconds at capture time */
    uint32_t point_num;                   /* 1-based sequence within the job */
    char code[GM_MEASURE_POINT_CODE_MAX]; /* free-form; not yet interpreted */
} MeasurePoint;

/* -------------------------------------------------------------------------
 * Fixed-capacity store -- one job's worth of captured points
 * ---------------------------------------------------------------------- */

typedef struct {
    MeasurePoint points[GM_MEASURE_POINTS_MAX];
    uint32_t count;
} MeasurePointStore;

/** Zeroes the store -- "no points captured yet". */
void measure_points_init(MeasurePointStore *store);

/**
 * Appends a point to the in-memory store. Does NOT write to disk -- call
 * measure_points_append_csv() separately (see below) to persist it.
 * Splitting the two means a caller can decide capture (always) vs.
 * persistence (only after a real fix) independently, and a unit test
 * can exercise the in-memory side with no filesystem at all.
 * point->point_num is overwritten with store->count + 1 before insertion
 * regardless of whatever the caller set -- the store is the single
 * source of truth for sequencing, not the caller.
 * Returns GM_OK on success, GM_ERR_NOMEM if the store is full.
 */
gm_status_t measure_points_add(MeasurePointStore *store, MeasurePoint point);

/* -------------------------------------------------------------------------
 * CSV persistence -- ~/geomark-data/projects/<project>/<job>/points.csv,
 * same directory job.ini already lives in (see job_metadata.h). CSV
 * rather than job_metadata.c's key=value INI shape because points are
 * naturally tabular (one row per point, append-only during a session),
 * not a flat set of named settings -- still the same fopen/fgets/
 * strerror/log_* error-handling conventions job_metadata.c established,
 * just a row format suited to the data.
 * ---------------------------------------------------------------------- */

/**
 * Appends one CSV row to the file at path, creating it (with a header
 * row) if it does not already exist. Called once per captured point
 * during a live session -- a crash mid-session loses at most the point
 * in progress, not every point captured so far, the same durability
 * goal SurveySession's per-session CSV already has.
 * Returns GM_OK on success, GM_ERR_IO if the file cannot be opened.
 */
gm_status_t measure_points_append_csv(const char *path, const MeasurePoint *point);

/**
 * Reads every row from path into store, overwriting whatever the store
 * currently holds. If the file does not exist, leaves store empty and
 * returns GM_OK (same "missing file is not an error" convention
 * job_metadata_load() uses) -- this is the expected case the first time
 * Measure Points is entered for a brand-new job.
 * Returns GM_ERR_PARSE if the file exists but its header row doesn't
 * match the expected column set, GM_OK otherwise.
 */
gm_status_t measure_points_load_csv(const char *path, MeasurePointStore *store);

/**
 * Fills buf with the full path to this job's points.csv, given the
 * resolved job directory (the same directory job.ini lives in -- see
 * job_create_screen.c / open_job_screen.c's existing
 * "%s/geomark-data/projects/%s/%s" construction). Caller-owned buffer,
 * same convention as every other path-building call site in this
 * codebase (none of which centralize this today; this helper exists so
 * Measure Points doesn't add a fourth copy of that snprintf).
 */
void measure_points_csv_path(const char *job_dir, char *buf, size_t buf_len);

/* -------------------------------------------------------------------------
 * Map projection -- WGS84 lat/lon -> a flat (east, north) pair in the
 * job's own coordinate system, for plotting on the map panel.
 *
 * Mirrors how Trimble Access's "Grid" coordinate view works: a point's
 * global (WGS84) position is stored once and projected through the
 * job's configured coordinate system at display time, not re-stored in
 * a second format (see coords.h's own doc comment on
 * coords_wgs84_to_nd_north() for GeoMark's existing version of this same
 * principle). GM_COORD_SYS_ND_NORTH gets a real surveyed projection via
 * the existing, verified coords_wgs84_to_nd_north() transform; every
 * other coord_sys (WGS84, UTM, Local Ground -- none have a working
 * forward transform yet, see coords.h) falls back to a local tangent-
 * plane approximation (equirectangular delta from origin, in meters) --
 * the same fallback Trimble itself uses for a job with no projection
 * defined ("Local geodetic coordinates"), simplified here since GeoMark
 * has no ellipsoid/datum-transform infrastructure built yet.
 * ---------------------------------------------------------------------- */

typedef struct {
    double east;  /* meters (local fallback) or feet (ND North, intl ft) */
    double north; /* same unit as east */
} MeasurePointsProjected;

/**
 * Projects (lat, lon) into the job's coordinate system.
 *
 * coord_sys == GM_COORD_SYS_ND_NORTH: real EPSG:2265 State Plane
 * easting/northing in international feet via coords_wgs84_to_nd_north().
 * origin_lat/origin_lon are ignored in this case -- the projection is
 * absolute, not relative to any one point.
 *
 * Every other coord_sys: equirectangular local-meters approximation
 * relative to (origin_lat, origin_lon) -- east = delta-longitude *
 * cos(origin_lat) * (meters per degree longitude), north = delta-
 * latitude * (meters per degree latitude). Accurate at survey scale
 * (a few km at most around the origin); not a real geodetic projection,
 * matching the "this is the honest local fallback, not real grid
 * coordinates" status those coord systems already have in coords.h.
 *
 * Returns GM_ERR_GENERIC if lat/lon are NaN or out is NULL (same
 * contract as coords_wgs84_to_nd_north()), GM_OK otherwise.
 */
gm_status_t measure_points_project(const gm_job_metadata_t *meta, double lat, double lon,
                                   double origin_lat, double origin_lon,
                                   MeasurePointsProjected *out);

#endif /* GEOMARK_MEASURE_POINTS_H */