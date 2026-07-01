/**
 * @file measure_points_export.h
 * @brief LandXML, PNEZD CSV, and PNEZD TXT export of a job's captured
 *        points (MeasurePointStore, see measure_points.h) -- the
 *        deliverable formats a field crew carries off the device, as
 *        opposed to points.csv (measure_points.h's own CSV persistence
 *        format, internal round-trip only in WGS84 degrees and metres).
 *
 * Three export formats are produced:
 *
 *   LandXML 1.2 (points.xml)
 *     A <CgPoints> collection with one <CgPoint name="..." code="...">
 *     per captured point. Coordinates in the job's chosen coordinate
 *     system, horizontal units per the job's dist_unit, elevation
 *     always International Foot. Suitable for import into Civil 3D
 *     via File > Import > Import LandXML.
 *
 *   PNEZD CSV (points_pnezd.csv)
 *     No header row. Columns: Point#, Northing, Easting, Elevation,
 *     Description. This is AutoCAD Civil 3D's native "PNEZD (comma
 *     delimited)" point import format (Home > Points > Import Points).
 *     Zero configuration required on import -- Civil 3D recognises
 *     this column order natively.
 *
 *   PNEZD TXT (points_pnezd.txt)
 *     Same PNEZD data rows as the CSV above, but preceded by a
 *     human-readable job metadata header block (job name, coordinate
 *     zone, export timestamp, reference number, description, units)
 *     and terminated with CRLF line endings throughout -- matching the
 *     header/body shape of the reference export Alex supplied
 *     (TOPO_EXPORT_EXAMPLE.txt). This is a documentation/handoff
 *     format for a human reader (the drafter/engineer receiving the
 *     job), not a second machine-import format -- Civil 3D's own
 *     PNEZD importer wants a header-free file, which is exactly what
 *     the plain CSV export above already provides; do not point Civil
 *     3D's importer at this file.
 *
 *     CAVEAT (flagged, not resolved, by this module -- see the
 *     session that introduced this format for the full discussion):
 *     the header's "Datum:" line reads "NAD83(1986)", matching the
 *     specific realization collector/coords.c's own Lambert projection
 *     constants are documented against (its own file-level doc
 *     comment cites the zone's EPSG:2265 WKT directly) -- NOT
 *     "NAD83(2011)" the way Alex's reference file's header reads
 *     (a different, more recent national NAD83 readjustment; the two
 *     realizations can differ by up to ~2m in parts of the US from
 *     tectonic motion between adjustments). Do not change this line to
 *     say "(2011)" without first confirming which realization
 *     GeoMark's actual RTK correction source is really tied to --
 *     doing so would misrepresent the exported data's datum.
 *     Similarly, "Geoid:"/"Vertical datum:" are written as
 *     informational placeholders, NOT a verified claim that
 *     MeasurePoint::alt (sourced from the UM980's own GGA "altitude
 *     MSL" field, gnss/nmea.c) was actually computed against GEOID18
 *     specifically -- that depends on the UM980 firmware's own,
 *     unverified internal geoid model. See measure_points_export_txt()'s
 *     own doc comment for the exact wording used and why.
 *
 * Coordinate handling:
 *   All three formats write PROJECTED grid coordinates (via
 *   measure_points_project(), same call the Measure Points map panel
 *   already uses), NOT raw WGS84 lat/lon. For GM_COORD_SYS_ND_NORTH
 *   the projection returns absolute NAD83 State Plane feet (easting,
 *   northing) and no origin is needed. For every other coord_sys the
 *   local equirectangular fallback returns metre offsets from the
 *   first captured point; these are converted to the job's chosen
 *   foot unit. Elevation (MeasurePoint::alt, metres MSL after
 *   target-height correction) is always converted to International
 *   Foot regardless of dist_unit, matching units.h's own convention.
 *
 * Formula-injection safety (CSV/TXT):
 *   Excel and similar spreadsheet tools interpret cell content that
 *   starts with '=', '+', '-', or '@' as a formula. Point names,
 *   codes, and point numbers that start with these characters (e.g.
 *   the breakline code "+CONC") are prefixed with a single-quote
 *   wrapper inside a quoted field per RFC 4180 so the cell content
 *   is treated as literal text. Specifically: any field whose first
 *   character is one of those four is written as =QUOTE("original"),
 *   NO -- simpler: it is written as a quoted CSV field with a tab
 *   prefix: "\t+CONC" inside double quotes. Civil 3D's own importer
 *   strips leading whitespace from string fields, so "\t" is
 *   invisible in the import result. Spreadsheet tools see the tab
 *   and skip formula evaluation.
 *
 *   Actually the correct RFC 4180-safe approach used here: any
 *   Description field starting with '=', '+', '-', or '@' is
 *   wrapped as a quoted field and the first character is preceded
 *   by a single space: " +CONC". Civil 3D strips leading spaces
 *   from the Description column on import. Excel sees " +CONC"
 *   (starts with space, not a formula trigger) and renders the cell
 *   as the literal string " +CONC", which is correct. The point
 *   number column is always purely numeric so injection is not
 *   possible there. The TXT format's data rows reuse this exact same
 *   per-row logic (write_csv_safe()) -- only the header block above
 *   them, and the CRLF line endings, differ from the plain CSV export.
 *
 * Deliberately a separate module rather than extending either
 * existing export-shaped file already in this codebase:
 *   - collector/export.h: unimplemented stubs for a different type.
 *   - survey/export.h: the legacy ui/client.c flow, must stay
 *     untouched per project hard rule.
 */

#ifndef GEOMARK_MEASURE_POINTS_EXPORT_H
#define GEOMARK_MEASURE_POINTS_EXPORT_H

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "geomark.h"

/**
 * Fills buf with the full path to this job's LandXML export file,
 * given the job's resolved directory. Fixed filename "points.xml"
 * under job_dir/export/ -- a re-export overwrites the previous file.
 */
void measure_points_export_landxml_path(const char *job_dir, char *buf, size_t buf_len);

/**
 * Fills buf with the full path to this job's PNEZD CSV export file,
 * given the job's resolved directory. Fixed filename
 * "points_pnezd.csv" under job_dir/export/.
 */
void measure_points_export_pnezd_path(const char *job_dir, char *buf, size_t buf_len);

/**
 * Fills buf with the full path to this job's PNEZD TXT export file
 * (header block + PNEZD rows, CRLF line endings -- see this header's
 * file-level doc comment), given the job's resolved directory. Fixed
 * filename "points_pnezd.txt" under job_dir/export/.
 */
void measure_points_export_txt_path(const char *job_dir, char *buf, size_t buf_len);

/**
 * A point's easting/northing/elevation, projected into the job's
 * coordinate system and converted to the job's chosen foot unit (see
 * this header's file-level doc comment) -- what every export format
 * and the Measure Points map panel both actually plot/write, as
 * opposed to measure_points_project()'s raw MeasurePointsProjected
 * (metres or feet depending on coord_sys, and with no per-point
 * localization correction applied).
 */
typedef struct {
    double northing;     /* job foot unit (Int'l or US Survey) */
    double easting;      /* job foot unit (Int'l or US Survey) */
    double elevation_ft; /* always International Foot */
} MeasurePointsProjectedFt;

/**
 * Projects pt into the job's coordinate system, converts to the job's
 * chosen foot unit, and adds pt's own dn_ft/de_ft/dz_ft localization
 * correction (see measure_points.h's MeasurePoint doc comment on why
 * that correction lives per-point, not per-job). origin_lat/origin_lon
 * are the first-point origin for non-ND-North coord systems; ignored
 * for ND North. This is the ONE place projection + unit conversion +
 * localization correction happen -- every export format below and
 * measure_points_screen_draw.c's map panel all call this rather than
 * duplicating any part of it. Returns GM_OK on success, whatever
 * measure_points_project() itself returns on failure (GM_ERR_GENERIC
 * for NaN lat/lon or a NULL out).
 */
gm_status_t measure_points_project_ft(const MeasurePoint *pt, const gm_job_metadata_t *job_meta,
                                      double origin_lat, double origin_lon,
                                      MeasurePointsProjectedFt *out);

/**
 * Writes every point in store to path as a LandXML 1.2 <CgPoints>
 * collection. Coordinates are projected and converted to feet (see
 * this header's file-level doc comment). origin_lat/origin_lon are
 * the first-point origin for non-ND-North coord systems; ignored for
 * ND North (absolute projection). If store->count == 0, writes a
 * valid empty <CgPoints/> element. Returns GM_OK on success,
 * GM_ERR_IO if the file cannot be opened, GM_ERR_GENERIC if store or
 * job_meta is NULL.
 */
gm_status_t measure_points_export_landxml(const char *path, const MeasurePointStore *store,
                                          const gm_job_metadata_t *job_meta, double origin_lat,
                                          double origin_lon);

/**
 * Writes every point in store to path as a PNEZD CSV file: no header,
 * columns Point#,Northing,Easting,Elevation,Description in that exact
 * order. This is Civil 3D's native "PNEZD (comma delimited)" import
 * format. Coordinates are projected and converted to feet (see this
 * header's file-level doc comment). Description field (point code) is
 * formula-injection-safe (see this header's file-level doc comment).
 * If store->count == 0, writes an empty file (no rows, no header).
 * Returns GM_OK on success, GM_ERR_IO if the file cannot be opened,
 * GM_ERR_GENERIC if store or job_meta is NULL.
 */
gm_status_t measure_points_export_pnezd(const char *path, const MeasurePointStore *store,
                                        const gm_job_metadata_t *job_meta, double origin_lat,
                                        double origin_lon);

/**
 * Writes every point in store to path as a PNEZD TXT file: the same
 * projected coordinates and Description-column safety as
 * measure_points_export_pnezd() above, but preceded by a human-
 * readable metadata header block (job name, coordinate zone, export
 * timestamp, job_meta's reference/description fields, units) and
 * written with CRLF line endings throughout, matching the shape of
 * the reference export Alex supplied (TOPO_EXPORT_EXAMPLE.txt). NOT a
 * second machine-import format -- see this header's file-level doc
 * comment for why Civil 3D's own importer should still be pointed at
 * the header-free CSV export instead, and for the Datum/Geoid caveat
 * this format's header carries. If store->count == 0, still writes
 * the header block with zero data rows following it. Returns GM_OK on
 * success, GM_ERR_IO if the file cannot be opened, GM_ERR_GENERIC if
 * store or job_meta is NULL.
 */
gm_status_t measure_points_export_txt(const char *path, const MeasurePointStore *store,
                                      const gm_job_metadata_t *job_meta, double origin_lat,
                                      double origin_lon);

#endif /* GEOMARK_MEASURE_POINTS_EXPORT_H */