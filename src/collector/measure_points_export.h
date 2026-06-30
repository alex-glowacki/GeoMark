/**
 * @file measure_points_export.h
 * @brief LandXML and PNEZD CSV export of a job's captured points
 *        (MeasurePointStore, see measure_points.h) -- the deliverable
 *        formats a field crew carries off the device, as opposed to
 *        points.csv (measure_points.h's own CSV persistence format,
 *        internal round-trip only in WGS84 degrees and metres).
 *
 * Two export formats are produced:
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
 * Coordinate handling:
 *   Both formats write PROJECTED grid coordinates (via
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
 * Formula-injection safety (CSV):
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
 *   possible there.
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

#endif /* GEOMARK_MEASURE_POINTS_EXPORT_H */