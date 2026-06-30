/**
 * @file measure_points_export.c
 * @brief Implementation -- see measure_points_export.h for design
 *        rationale, coordinate handling, and formula-injection safety.
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
 * Export subdirectory creation
 * ---------------------------------------------------------------------- */

static bool mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("measure_points_export: cannot create directory %s: %s",
                 path, strerror(errno));
        return false;
    }
    return true;
}

static bool ensure_export_dir_for(const char *path)
{
    char dir[640];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return true;
    *slash = '\0';
    return mkdir_if_missing(dir);
}

/* -------------------------------------------------------------------------
 * Coordinate-system label strings (for LandXML comment and <Units>)
 * ---------------------------------------------------------------------- */

static const char *const EXPORT_COORD_SYS_NAMES[] = {
    "WGS84 Geographic",
    "UTM (auto zone)",
    "Local / Site Ground",
    "NAD83(1986) ND State Plane North (EPSG:2265)",
};

/* -------------------------------------------------------------------------
 * Path helpers
 * ---------------------------------------------------------------------- */

void measure_points_export_landxml_path(const char *job_dir, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/export/points.xml", job_dir);
}

void measure_points_export_pnezd_path(const char *job_dir, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/export/points_pnezd.csv", job_dir);
}

/* -------------------------------------------------------------------------
 * Shared projection + unit conversion
 *
 * Returns projected northing/easting in the job's chosen foot unit
 * and elevation always in International Foot. See measure_points_
 * export.h's file-level doc comment for the full unit-handling rules.
 * ---------------------------------------------------------------------- */

typedef struct {
    double northing;      /* job foot unit (Int'l or US Survey) */
    double easting;       /* job foot unit (Int'l or US Survey) */
    double elevation_ft;  /* always International Foot */
} ExportedPoint;

static gm_status_t project_for_export(const MeasurePoint *pt,
                                      const gm_job_metadata_t *job_meta,
                                      double origin_lat, double origin_lon,
                                      ExportedPoint *out)
{
    MeasurePointsProjected proj;
    gm_status_t rc = measure_points_project(job_meta, pt->lat, pt->lon,
                                            origin_lat, origin_lon, &proj);
    if (rc != GM_OK)
        return rc;

    if (job_meta->coord_sys == GM_COORD_SYS_ND_NORTH) {
        /* measure_points_project() returns State Plane feet directly
         * for ND North -- use as-is, do NOT re-convert. */
        out->northing = proj.north;
        out->easting  = proj.east;
    } else {
        /* measure_points_project() returns metres for every other
         * coord_sys (local equirectangular fallback). Convert to the
         * job's chosen horizontal foot unit. */
        if (job_meta->dist_unit == GM_DIST_UNIT_US_SURVEY_FOOT) {
            out->northing = gm_m_to_survey_ft(proj.north);
            out->easting  = gm_m_to_survey_ft(proj.east);
        } else {
            out->northing = gm_m_to_intl_ft(proj.north);
            out->easting  = gm_m_to_intl_ft(proj.east);
        }
    }

    /* pt->alt is the corrected ground elevation in metres (raw antenna
     * altitude minus target height -- see measure_points.h's MeasurePoint
     * doc comment). Always convert to International Foot for elevation,
     * matching units.h's own convention. */
    out->elevation_ft = gm_m_to_intl_ft(pt->alt);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * Formula-injection-safe CSV field writer
 *
 * Excel, LibreOffice Calc, and Google Sheets treat cell content that
 * begins with '=', '+', '-', or '@' as a formula. Point codes like
 * "+CONC" or "-RCPF" (breakline linking codes) trigger this, producing
 * a #NAME? error. Fix: if the field starts with one of those four
 * characters, prefix a single space inside a quoted field -- the space
 * makes the cell start with a non-formula character so spreadsheet
 * tools render it as literal text, while Civil 3D's PNEZD importer
 * strips leading whitespace from the Description column on import, so
 * the space is invisible in the imported data.
 *
 * Fields that do NOT start with a formula trigger character are written
 * unquoted (no quoting overhead for the common case). Fields that DO
 * trigger injection are written as: "<SP><original>" inside RFC 4180
 * double-quotes, e.g. "+CONC" -> " +CONC" (quoted).
 *
 * This is called only for the PNEZD Description column (point code).
 * Point names and point numbers never trigger injection in practice
 * (names are purely numeric auto-incremented integers; point numbers
 * are unsigned integers) but names are also passed through this
 * function for completeness.
 * ---------------------------------------------------------------------- */

static void write_csv_safe(FILE *f, const char *s)
{
    if (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@') {
        /* Prefix a single space inside RFC 4180 double-quoted field.
         * Neither the field content nor the space itself can contain a
         * double-quote (the on-screen keyboard's closed character set
         * does not include '"', see ui/core/keyboard.h), so no
         * further escaping is needed. */
        fprintf(f, "\" %s\"", s);
    } else {
        fputs(s, f);
    }
}

/* -------------------------------------------------------------------------
 * XML attribute escaping (LandXML only)
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * LandXML export
 * ---------------------------------------------------------------------- */

gm_status_t measure_points_export_landxml(const char *path,
                                          const MeasurePointStore *store,
                                          const gm_job_metadata_t *job_meta,
                                          double origin_lat, double origin_lon)
{
    if (!store || !job_meta)
        return GM_ERR_GENERIC;

    if (!ensure_export_dir_for(path))
        return GM_ERR_IO;

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("measure_points_export_landxml: cannot open '%s': %s",
                  path, strerror(errno));
        return GM_ERR_IO;
    }

    const char *coord_sys_name =
        EXPORT_COORD_SYS_NAMES[
            (unsigned)job_meta->coord_sys < 4 ? (unsigned)job_meta->coord_sys : 0];

    /* linearUnit: "foot" = International Foot (LandXML 1.2 schema
     * enumeration); "USSurveyFoot" = US Survey Foot. ND North forces
     * International Foot via job_metadata_coerce_units(). */
    const char *linear_unit =
        (job_meta->coord_sys == GM_COORD_SYS_ND_NORTH ||
         job_meta->dist_unit == GM_DIST_UNIT_INTL_FOOT)
            ? "foot" : "USSurveyFoot";

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
    fputs("<LandXML version=\"1.2\" "
          "xmlns=\"http://www.landxml.org/schema/LandXML-1.2\">\n", f);
    /* XML comments may not contain "--"; use a single dash. */
    fprintf(f, "  <!-- GeoMark export: %s, %s -->\n",
            coord_sys_name, linear_unit);
    fprintf(f, "  <Units>\n"
               "    <Imperial linearUnit=\"%s\" areaUnit=\"squareFoot\""
               " volumeUnit=\"cubicFoot\" temperatureUnit=\"fahrenheit\""
               " pressureUnit=\"PSI\"/>\n"
               "  </Units>\n",
            linear_unit);
    fputs("  <Application name=\"GeoMark\" desc=\"DIY RTK GNSS survey system\"/>\n", f);

    fputs("  <CgPoints>\n", f);
    for (uint32_t i = 0; i < store->count; i++) {
        const MeasurePoint *pt = &store->points[i];

        ExportedPoint ep;
        if (project_for_export(pt, job_meta, origin_lat, origin_lon, &ep) != GM_OK) {
            log_warn("measure_points_export_landxml: point #%u failed to project"
                     " -- skipped", pt->point_num);
            continue;
        }

        /* LandXML CgPoint coordinate text content order: N E Z
         * (northing easting elevation), per the LandXML 1.2 schema's
         * PointType definition. */
        fprintf(f, "    <CgPoint name=\"");
        write_xml_escaped(f, pt->name);
        fputs("\" code=\"", f);
        write_xml_escaped(f, pt->code);
        fprintf(f, "\">%.4f %.4f %.4f</CgPoint>\n",
                ep.northing, ep.easting, ep.elevation_ft);
    }
    fputs("  </CgPoints>\n", f);
    fputs("</LandXML>\n", f);

    fclose(f);
    log_info("measure_points_export_landxml: wrote %u point(s) to '%s'",
             store->count, path);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * PNEZD CSV export
 *
 * Column order: Point#, Northing, Easting, Elevation, Description
 * No header row. This is Civil 3D's native "PNEZD (comma delimited)"
 * point import format -- File > Import > Import Points, format
 * "PNEZD (comma delimited)".
 *
 * All coordinate values are in feet (see project_for_export() above).
 * The Description column (point code) is written via write_csv_safe()
 * to prevent formula injection in spreadsheet tools (see that
 * function's doc comment).
 * ---------------------------------------------------------------------- */

gm_status_t measure_points_export_pnezd(const char *path,
                                        const MeasurePointStore *store,
                                        const gm_job_metadata_t *job_meta,
                                        double origin_lat, double origin_lon)
{
    if (!store || !job_meta)
        return GM_ERR_GENERIC;

    if (!ensure_export_dir_for(path))
        return GM_ERR_IO;

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("measure_points_export_pnezd: cannot open '%s': %s",
                  path, strerror(errno));
        return GM_ERR_IO;
    }

    /* No header row -- Civil 3D's PNEZD importer expects raw data rows
     * only; a header would be imported as a bogus point #0. */

    for (uint32_t i = 0; i < store->count; i++) {
        const MeasurePoint *pt = &store->points[i];

        ExportedPoint ep;
        if (project_for_export(pt, job_meta, origin_lat, origin_lon, &ep) != GM_OK) {
            log_warn("measure_points_export_pnezd: point #%u failed to project"
                     " -- skipped", pt->point_num);
            continue;
        }

        /* Point number is always a positive integer -- no injection
         * risk. Northing, Easting, Elevation are floating-point
         * numerics -- no injection risk. Only the Description
         * (code) field is written through write_csv_safe(). */
        fprintf(f, "%u,%.4f,%.4f,%.4f,", pt->point_num,
                ep.northing, ep.easting, ep.elevation_ft);
        write_csv_safe(f, pt->code);
        fputc('\n', f);
    }

    fclose(f);
    log_info("measure_points_export_pnezd: wrote %u point(s) to '%s'",
             store->count, path);
    return GM_OK;
}