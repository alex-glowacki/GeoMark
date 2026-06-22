/**
 * @file measure_points_export.c
 * @brief Implementation -- see measure_points_export.h for design
 *        rationale.
 */

#define _GNU_SOURCE

#include "collector/measure_points_export.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/log.h"
#include "util/units.h"

/* -------------------------------------------------------------------------
 * export/ subdirectory creation -- job_dir always exists by the time
 * this module runs (JobContext only ever points at an already-created
 * job, see job_context.h), but job_dir/export/ does not until the
 * first export. Same single-level mkdir_if_missing() convention
 * new_project_screen.c/job_create_screen.c already each define for
 * their own multi-level directory creation, here for one level.
 * ---------------------------------------------------------------------- */

static bool mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("measure_points_export: cannot create directory %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

/**
 * Ensures the directory containing path exists (single level only --
 * sufficient here since path is always job_dir/export/<file>, and
 * job_dir itself always already exists by the time this module runs,
 * see this function's own doc comment below). Derives that directory
 * by truncating path at its last '/', rather than taking job_dir as a
 * separate parameter -- both export functions below already receive
 * a fully-built path (from measure_points_export_landxml_path()/
 * _csv_path()), and re-deriving the parent from it avoids a second,
 * redundant job_dir parameter whose only use would be reconstructing
 * the exact same export_dir string those path helpers already built.
 */
static bool ensure_export_dir_for(const char *path)
{
    char dir[640];
    snprintf(dir, sizeof(dir), "%s", path);

    char *slash = strrchr(dir, '/');
    if (!slash)
        return true; /* no directory component -- nothing to create */
    *slash = '\0';

    return mkdir_if_missing(dir);
}

/* -------------------------------------------------------------------------
 * Coordinate-system / unit label strings -- same option-array convention
 * job_create_screen.c's COORD_SYS_OPTIONS[]/DIST_UNIT_OPTIONS[] already
 * established for its dropdowns, declared again here (rather than
 * exported from that file) since neither array is part of that screen's
 * public header -- this module has no reason to reach into a UI screen's
 * private file-local constants for a handful of labels, the same
 * "own narrow constants, not a shared header for one user" stance this
 * file's own doc comment already takes for the rest of its design.
 * Indexed directly by the enum value, matching that same convention.
 * ---------------------------------------------------------------------- */

static const char *const EXPORT_COORD_SYS_NAMES[] = {
    "WGS84 Geographic",
    "UTM (auto zone)",
    "Local / Site Ground",
    "NAD83(1986) ND State Plane North (EPSG:2265)",
};

static const char *const EXPORT_DIST_UNIT_NAMES[] = {
    "US Survey Foot",
    "International Foot",
};

/* -------------------------------------------------------------------------
 * Path helpers -- mirrors measure_points_csv_path()'s own snprintf
 * pattern exactly, see that function (measure_points.c) and this
 * header's own doc comment on why these are fixed names, not
 * user-typed.
 * ---------------------------------------------------------------------- */

void measure_points_export_landxml_path(const char *job_dir, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/export/points.xml", job_dir);
}

void measure_points_export_csv_path(const char *job_dir, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/export/points_export.csv", job_dir);
}

/* -------------------------------------------------------------------------
 * Shared projection + unit-conversion step -- one point's northing/
 * easting/elevation in the job's chosen unit, accounting for
 * measure_points_project()'s own mixed-unit return (see this module's
 * header doc comment). Used by both export functions below so the
 * "don't double-convert an already-feet ND North value" rule lives in
 * exactly one place.
 * ---------------------------------------------------------------------- */

typedef struct {
    double northing;
    double easting;
    double elevation_ft; /* always International Foot, see header doc */
} ExportedPoint;

static gm_status_t project_for_export(const MeasurePoint *pt, const gm_job_metadata_t *job_meta,
                                      double origin_lat, double origin_lon,
                                      ExportedPoint *out)
{
    MeasurePointsProjected proj;
    gm_status_t rc = measure_points_project(job_meta, pt->lat, pt->lon, origin_lat, origin_lon,
                                            &proj);
    if (rc != GM_OK)
        return rc;

    if (job_meta->coord_sys == GM_COORD_SYS_ND_NORTH) {
        /* Already International Foot -- use as-is, do NOT re-convert. */
        out->easting  = proj.east;
        out->northing = proj.north;
    } else {
        /* proj.east/proj.north are metres (local-fallback projection,
         * see measure_points_project()'s own doc comment) -- convert
         * per the job's chosen horizontal unit. */
        if (job_meta->dist_unit == GM_DIST_UNIT_US_SURVEY_FOOT) {
            out->easting  = gm_m_to_survey_ft(proj.east);
            out->northing = gm_m_to_survey_ft(proj.north);
        } else {
            out->easting  = gm_m_to_intl_ft(proj.east);
            out->northing = gm_m_to_intl_ft(proj.north);
        }
    }

    /* Elevation: always International Foot regardless of dist_unit --
     * see header doc comment. pt->alt is the corrected ground
     * elevation in metres (measure_points.h's MeasurePoint doc
     * comment). */
    out->elevation_ft = gm_m_to_intl_ft(pt->alt);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * LandXML export
 * ---------------------------------------------------------------------- */

/**
 * Minimal XML-attribute escaping for point name/code, which are
 * free-typed field-crew text (measure_points.h's MeasurePoint doc
 * comment) and so may legitimately contain '"', '&', '<', or '>'.
 * Writes the escaped form of s directly to f. Only the five characters
 * XML attribute values require escaping for are handled -- this is not
 * a general-purpose XML writer, just enough correctness for the two
 * free-text fields this export ever places inside an attribute.
 */
static void write_xml_escaped(FILE *f, const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        switch (*p) {
        case '&':  fputs("&amp;",  f); break;
        case '<':  fputs("&lt;",   f); break;
        case '>':  fputs("&gt;",   f); break;
        case '"':  fputs("&quot;", f); break;
        case '\'': fputs("&apos;", f); break;
        default:   fputc(*p, f);       break;
        }
    }
}

gm_status_t measure_points_export_landxml(const char *path, const MeasurePointStore *store,
                                          const gm_job_metadata_t *job_meta,
                                          double origin_lat, double origin_lon)
{
    if (!store || !job_meta)
        return GM_ERR_GENERIC;

    if (!ensure_export_dir_for(path))
        return GM_ERR_IO;

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("measure_points_export_landxml: cannot open '%s': %s", path, strerror(errno));
        return GM_ERR_IO;
    }

    const char *unit_name =
        EXPORT_DIST_UNIT_NAMES[(job_meta->dist_unit == GM_DIST_UNIT_US_SURVEY_FOOT) ? 0 : 1];
    const char *coord_sys_name =
        EXPORT_COORD_SYS_NAMES[(unsigned)job_meta->coord_sys < 4 ? (unsigned)job_meta->coord_sys
                                                                  : 0];

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
    fputs("<LandXML version=\"1.2\" "
          "xmlns=\"http://www.landxml.org/schema/LandXML-1.2\">\n", f);
    fprintf(f, "  <!-- GeoMark export: coordinate system %s, linear unit %s -->\n",
            coord_sys_name, unit_name);
    /* linearUnit/areaUnit/volumeUnit are required attributes of <Units>'s
     * child elements per the LandXML schema; "USSurveyFoot"/"foot" are
     * the schema's own enumerated linearUnit values for these two foot
     * definitions (areaSqFoot avoided -- GeoMark exports points only,
     * never areas, so a literal but otherwise-unused area unit would
     * just be noise; left as the schema's own foot-derived default). */
    fprintf(f, "  <Units>\n"
               "    <Imperial linearUnit=\"%s\" areaUnit=\"squareFoot\" "
               "volumeUnit=\"cubicFoot\" temperatureUnit=\"fahrenheit\" "
               "pressureUnit=\"PSI\"/>\n"
               "  </Units>\n",
            (job_meta->coord_sys == GM_COORD_SYS_ND_NORTH ||
             job_meta->dist_unit == GM_DIST_UNIT_INTL_FOOT)
                ? "foot"
                : "USSurveyFoot");
    fputs("  <Application name=\"GeoMark\" desc=\"DIY RTK GNSS survey system\"/>\n", f);

    fputs("  <CgPoints>\n", f);
    for (uint32_t i = 0; i < store->count; i++) {
        const MeasurePoint *pt = &store->points[i];

        ExportedPoint ep;
        gm_status_t rc = project_for_export(pt, job_meta, origin_lat, origin_lon, &ep);
        if (rc != GM_OK) {
            log_warn("measure_points_export_landxml: point #%u failed to project -- skipped",
                     pt->point_num);
            continue;
        }

        fprintf(f, "    <CgPoint name=\"");
        write_xml_escaped(f, pt->name);
        fputs("\" code=\"", f);
        write_xml_escaped(f, pt->code);
        fprintf(f, "\">%.4f %.4f %.4f</CgPoint>\n", ep.northing, ep.easting, ep.elevation_ft);
    }
    fputs("  </CgPoints>\n", f);

    fputs("</LandXML>\n", f);

    fclose(f);
    log_info("measure_points_export_landxml: wrote %u point(s) to '%s'", store->count, path);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * CSV export
 * ---------------------------------------------------------------------- */

static const char *const EXPORT_CSV_HEADER = "Point name,Code,Northing,Easting,Elevation\n";

gm_status_t measure_points_export_csv(const char *path, const MeasurePointStore *store,
                                      const gm_job_metadata_t *job_meta,
                                      double origin_lat, double origin_lon)
{
    if (!store || !job_meta)
        return GM_ERR_GENERIC;

    if (!ensure_export_dir_for(path))
        return GM_ERR_IO;

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("measure_points_export_csv: cannot open '%s': %s", path, strerror(errno));
        return GM_ERR_IO;
    }

    fputs(EXPORT_CSV_HEADER, f);

    for (uint32_t i = 0; i < store->count; i++) {
        const MeasurePoint *pt = &store->points[i];

        ExportedPoint ep;
        gm_status_t rc = project_for_export(pt, job_meta, origin_lat, origin_lon, &ep);
        if (rc != GM_OK) {
            log_warn("measure_points_export_csv: point #%u failed to project -- skipped",
                     pt->point_num);
            continue;
        }

        /* name/code are free-form (measure_points.h's MeasurePoint doc
         * comment) and could in principle contain a comma -- this
         * codebase has no CSV-quoting convention anywhere (points.csv's
         * own measure_points_append_csv() also writes name/code
         * unquoted, see measure_points.c), so this matches that
         * existing precedent rather than introducing a new one only
         * this file follows. */
        fprintf(f, "%s,%s,%.4f,%.4f,%.4f\n", pt->name, pt->code, ep.northing, ep.easting,
                ep.elevation_ft);
    }

    fclose(f);
    log_info("measure_points_export_csv: wrote %u point(s) to '%s'", store->count, path);
    return GM_OK;
}