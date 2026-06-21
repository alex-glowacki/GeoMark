/**
 * @file measure_points.c
 * @brief Implementation -- see measure_points.h for design rationale.
 */

#define _GNU_SOURCE

#include "collector/measure_points.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "collector/coords.h"
#include "util/log.h"

/** Local to this file, same convention as coords.c's own GM_PI -- no
 *  shared math constants header exists in this codebase yet, and one
 *  is not worth introducing for a single constant used in one place. */
#define GM_MEASURE_PI 3.14159265358979323846

/** Mean Earth radius in meters (WGS84 mean radius, IUGG value) -- used
 *  only by the local equirectangular fallback projection, not by the
 *  real ND North transform (which has its own GRS80 ellipsoid constants
 *  in coords.c). Adequate for the few-km-scale local approximation this
 *  fallback is explicitly documented as being, not a geodetic-grade
 *  value. */
#define GM_MEASURE_EARTH_RADIUS_M 6371000.0

#define MEASURE_POINTS_CSV_MAX_LINE 256

/* -------------------------------------------------------------------------
 * In-memory store
 * ---------------------------------------------------------------------- */

void measure_points_init(MeasurePointStore *store)
{
    memset(store, 0, sizeof(*store));
}

gm_status_t measure_points_add(MeasurePointStore *store, MeasurePoint point)
{
    if (store->count >= GM_MEASURE_POINTS_MAX) {
        log_error("measure_points_add: store full (%u points)", GM_MEASURE_POINTS_MAX);
        return GM_ERR_NOMEM;
    }

    point.point_num = store->count + 1;
    store->points[store->count] = point;
    store->count++;
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * CSV persistence
 * ---------------------------------------------------------------------- */

/* Column order is fixed and must match between the header row written
 * here and the header row checked in measure_points_load_csv() below. */
static const char *CSV_HEADER =
    "point_num,timestamp,lat,lon,alt,fix_quality,hdop,num_sats,code\n";

void measure_points_csv_path(const char *job_dir, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/points.csv", job_dir);
}

gm_status_t measure_points_append_csv(const char *path, const MeasurePoint *point)
{
    /* Determine whether the file already exists (and so already has a
     * header) before opening in append mode -- fopen's "a" mode creates
     * the file if absent but gives no way to tell after the fact
     * whether it just did so. */
    bool exists;
    {
        FILE *probe = fopen(path, "r");
        exists = (probe != NULL);
        if (probe) fclose(probe);
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        log_error("measure_points_append_csv: cannot open '%s': %s", path, strerror(errno));
        return GM_ERR_IO;
    }

    if (!exists)
        fputs(CSV_HEADER, f);

    fprintf(f, "%u,%lld,%.8f,%.8f,%.3f,%u,%.2f,%u,%s\n",
            point->point_num,
            (long long)point->timestamp,
            point->lat,
            point->lon,
            point->alt,
            (unsigned)point->fix_quality,
            point->hdop,
            (unsigned)point->num_sats,
            point->code);

    fclose(f);
    log_info("measure_points_append_csv: wrote point #%u to '%s'", point->point_num, path);
    return GM_OK;
}

gm_status_t measure_points_load_csv(const char *path, MeasurePointStore *store)
{
    measure_points_init(store);

    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("measure_points_load_csv: cannot open '%s': %s -- starting empty",
                 path, strerror(errno));
        return GM_OK;
    }

    char line[MEASURE_POINTS_CSV_MAX_LINE];

    /* Header row -- must match exactly, same strictness coords.h's own
     * forward-only-transform comment implies for this codebase ("no
     * range check is performed... it simply becomes a poor
     * approximation" is the documented tolerance level; a header
     * mismatch here is a real format problem, not a tolerance case). */
    if (!fgets(line, sizeof(line), f)) {
        log_warn("measure_points_load_csv: '%s' is empty -- starting empty", path);
        fclose(f);
        return GM_OK;
    }
    if (strcmp(line, CSV_HEADER) != 0) {
        log_error("measure_points_load_csv: '%s' header does not match expected columns",
                  path);
        fclose(f);
        return GM_ERR_PARSE;
    }

    int lineno = 1;
    while (fgets(line, sizeof(line), f)) {
        lineno++;

        if (store->count >= GM_MEASURE_POINTS_MAX) {
            log_warn("measure_points_load_csv: '%s' has more than %u points -- truncating",
                     path, GM_MEASURE_POINTS_MAX);
            break;
        }

        MeasurePoint pt;
        memset(&pt, 0, sizeof(pt));

        unsigned point_num, fix_quality, num_sats;
        long long ts;
        int code_offset = 0;
        /* Only the first 8 (fixed-format, comma-delimited) numeric
         * columns are parsed via sscanf, with %n capturing the byte
         * offset immediately after the 8th column's trailing comma.
         * code is then taken as everything from that offset to the
         * end of the line, trimmed of its trailing newline -- NOT
         * matched via %s, which requires at least one non-whitespace
         * character and therefore rejects the common case of an empty
         * code field (the row this writes when no code was entered:
         * "...,12,\n" -- nothing for %s to consume after the last
         * comma, so n would come back 8 instead of 9 every time code
         * is empty, the actual bug this replaced). */
        int n = sscanf(line, "%u,%lld,%lf,%lf,%lf,%u,%lf,%u,%n",
                      &point_num, &ts, &pt.lat, &pt.lon, &pt.alt,
                      &fix_quality, &pt.hdop, &num_sats, &code_offset);

        if (n != 8 || code_offset == 0) {
            log_warn("measure_points_load_csv: '%s':%d: malformed row -- skipped",
                     path, lineno);
            continue;
        }

        size_t code_len = strlen(line + code_offset);
        while (code_len > 0 && (line[code_offset + (long)code_len - 1] == '\n'
                                || line[code_offset + (long)code_len - 1] == '\r'))
            code_len--;
        if (code_len >= sizeof(pt.code))
            code_len = sizeof(pt.code) - 1;
        memcpy(pt.code, line + code_offset, code_len);
        pt.code[code_len] = '\0';

        pt.point_num   = point_num;
        pt.timestamp   = (time_t)ts;
        pt.fix_quality = (uint8_t)fix_quality;
        pt.num_sats    = (uint8_t)num_sats;

        store->points[store->count] = pt;
        store->count++;
    }

    fclose(f);
    log_info("measure_points_load_csv: loaded %u point(s) from '%s'", store->count, path);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * Map projection
 * ---------------------------------------------------------------------- */

gm_status_t measure_points_project(const gm_job_metadata_t *meta, double lat, double lon,
                                   double origin_lat, double origin_lon,
                                   MeasurePointsProjected *out)
{
    if (!meta || !out || isnan(lat) || isnan(lon))
        return GM_ERR_GENERIC;

    if (meta->coord_sys == GM_COORD_SYS_ND_NORTH) {
        gm_state_plane_t sp;
        gm_status_t rc = coords_wgs84_to_nd_north(lat, lon, &sp);
        if (rc != GM_OK)
            return rc;
        out->east  = sp.easting_ft;
        out->north = sp.northing_ft;
        return GM_OK;
    }

    /* Local equirectangular fallback -- see measure_points.h's doc
     * comment on this function for why every other coord_sys lands
     * here (no working forward transform exists for them yet). */
    if (isnan(origin_lat) || isnan(origin_lon))
        return GM_ERR_GENERIC;

    const double origin_phi = origin_lat * GM_MEASURE_PI / 180.0;
    const double dlat = lat - origin_lat;
    const double dlon = lon - origin_lon;

    const double m_per_deg_lat = (GM_MEASURE_PI / 180.0) * GM_MEASURE_EARTH_RADIUS_M;
    const double m_per_deg_lon = m_per_deg_lat * cos(origin_phi);

    out->north = dlat * m_per_deg_lat;
    out->east  = dlon * m_per_deg_lon;
    return GM_OK;
}