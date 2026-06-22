/**
 * @file measure_points_export.h
 * @brief LandXML and CSV export of a job's captured points
 *        (MeasurePointStore, see measure_points.h) -- the deliverable
 *        formats a field crew actually carries off the device, as
 *        opposed to points.csv (measure_points.h's own CSV
 *        persistence), which is GeoMark's internal round-trip format
 *        (WGS84 degrees, metres, one column set fixed by what
 *        measure_points_load_csv() parses back in).
 *
 * Deliberately a new, separate module rather than extending either
 * existing export-shaped file already in this codebase:
 *
 *   - collector/export.h (export_csv/export_dxf): both functions are
 *     unimplemented "TODO: Phase 3" stubs tied to collector/points.h's
 *     gm_point_store_t, which itself has no working behavior anywhere
 *     in this codebase (see measure_points.h's own doc comment on the
 *     same dead module). Nothing to inherit.
 *   - survey/export.h (export_write_header/_write_point/_scp): real,
 *     working code, but it belongs to the legacy ui/client.c capture
 *     flow that must remain completely untouched (project hard rule)
 *     -- it operates on SurveyPoint/FILE* mid-session streaming with a
 *     different column order (LAT,LON,ALT,POINT_CODE,...) fixed by
 *     that flow's own spec, and a hardcoded SCP destination specific
 *     to that workflow. Measure Points' export is a one-shot, whole-
 *     job write triggered by a button, not a per-shot append during
 *     capture -- a different shape entirely.
 *
 * This is the third occurrence of the same "narrow module, own schema"
 * precedent job_metadata.h and measure_points.h's own file doc
 * comments already established for this codebase -- see either for
 * the general rationale.
 *
 * Coordinates: both formats write the job's PROJECTED grid
 * coordinates (via measure_points_project(), same call Measure
 * Points' map panel already uses), not raw WGS84 lat/lon -- a field
 * deliverable should match whatever grid the job's coord_sys
 * actually is, not force the recipient to reproject. origin_lat/
 * origin_lon are passed through unchanged to measure_points_project()
 * for every point (same parameters that function itself takes) --
 * ignored when coord_sys is GM_COORD_SYS_ND_NORTH (that projection is
 * absolute, see measure_points_project()'s own doc comment), required
 * for every other coord_sys's local-fallback projection. Caller-
 * supplied rather than recomputed here, matching every other reader
 * of MeasurePointStore in this codebase (measure_points_screen.c's
 * on_capture_point()/reload_job_data()) already establishing "first
 * point in the store sets the origin" as the screen's own concern,
 * not something a narrow export module should re-derive on its own.
 *
 * Unit handling: measure_points_project() itself returns MIXED units
 * depending on coord_sys (see that function's own header doc comment
 * on MeasurePointsProjected -- feet for GM_COORD_SYS_ND_NORTH, METRES
 * for every other coord_sys's local-fallback projection). This module
 * accounts for that directly: the ND North branch's output is used
 * as-is (already International Foot, already correct); every other
 * branch's metres output is converted via job_meta->dist_unit (US
 * Survey Foot or International Foot, see units.h/job_metadata.h) --
 * never blindly re-applying a foot conversion to a value that might
 * already be in feet. Elevation always uses the International Foot
 * regardless of dist_unit, matching units.h's own file-level
 * convention that vertical measurements are never in the US Survey
 * Foot. For GM_COORD_SYS_ND_NORTH this is moot --
 * job_metadata_coerce_units() already forces dist_unit to
 * International Foot for that coord_sys, so horizontal and vertical
 * agree automatically; for every other coord_sys, a job's horizontal
 * unit choice and elevation's fixed International Foot convention can
 * legitimately differ within the same export, same as every other
 * screen/module in this codebase already allows.
 *
 * Both formats are write-only: nothing in GeoMark re-ingests its own
 * LandXML or export CSV output (this mirrors job.ini/points.csv being
 * the only files anything in this codebase reads back -- export
 * output is a one-way deliverable for downstream tools (CAD/GIS for
 * LandXML, anything spreadsheet-shaped for CSV), not a GeoMark-native
 * persistence format).
 */

#ifndef GEOMARK_MEASURE_POINTS_EXPORT_H
#define GEOMARK_MEASURE_POINTS_EXPORT_H

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "geomark.h"

/**
 * Fills buf with the full path to this job's LandXML export file,
 * given the job's resolved directory (same job_dir every other path
 * helper in this codebase takes -- see measure_points_csv_path()).
 * Fixed name, not user-typed (see export_screen.h's file doc comment
 * for why) -- a re-export after more points are captured overwrites
 * the previous file, matching points.csv's own
 * "this job has exactly one of these" precedent.
 */
void measure_points_export_landxml_path(const char *job_dir, char *buf, size_t buf_len);

/**
 * Fills buf with the full path to this job's export CSV file, given
 * the job's resolved directory. Distinct filename from points.csv
 * (measure_points.h's internal round-trip file) -- see this header's
 * file-level doc comment on why the two are different files with
 * different column sets, not the same file read back for a second
 * purpose.
 */
void measure_points_export_csv_path(const char *job_dir, char *buf, size_t buf_len);

/**
 * Writes every point in store to path as a single LandXML 1.2
 * <CgPoints> collection -- <Units>, <Application>, and one <CgPoint
 * name="..." code="..."> per point, coordinate text content in the
 * LandXML-mandated "northing easting elevation" order (verified
 * against the LandXML 1.2 schema's own PointType convention, see this
 * header's file doc comment). No <Surfaces>/<Alignments>/other
 * elements -- this is a point-cloud-only export, the same minimal
 * subset Trimble Access itself produces when exporting just a job's
 * point list rather than a full survey deliverable.
 *
 * origin_lat/origin_lon: see this header's file-level doc comment --
 * required for non-ND-North coord systems, ignored for ND North.
 *
 * If store->count == 0, still writes a valid (empty) <CgPoints/>
 * collection rather than failing -- an empty export is a legitimate
 * "nothing captured yet" result, not an error condition, matching
 * measure_points_load_csv()'s own "missing/empty is not an error"
 * convention elsewhere in this codebase.
 *
 * Returns GM_OK on success, GM_ERR_IO if path cannot be opened for
 * writing, GM_ERR_GENERIC if store or job_meta is NULL.
 */
gm_status_t measure_points_export_landxml(const char *path, const MeasurePointStore *store,
                                          const gm_job_metadata_t *job_meta, double origin_lat,
                                          double origin_lon);

/**
 * Writes every point in store to path as comma-delimited CSV: one
 * header row (Point name,Code,Northing,Easting,Elevation) followed by
 * one row per point -- the same five-field column order and naming
 * Trimble Access's own predefined CSV export format uses (Point
 * name/Point code/Northing/Easting/Elevation), so the output opens
 * correctly in any tool already set up to expect that shape. Distinct
 * from points.csv's internal round-trip format (lat/lon/raw_alt/
 * fix_quality/etc in WGS84 degrees and metres) -- see this header's
 * file-level doc comment.
 *
 * origin_lat/origin_lon: see this header's file-level doc comment --
 * required for non-ND-North coord systems, ignored for ND North.
 *
 * If store->count == 0, still writes a header-only file rather than
 * failing -- same "empty is not an error" stance as the LandXML
 * export above.
 *
 * Returns GM_OK on success, GM_ERR_IO if path cannot be opened for
 * writing, GM_ERR_GENERIC if store or job_meta is NULL.
 */
gm_status_t measure_points_export_csv(const char *path, const MeasurePointStore *store,
                                      const gm_job_metadata_t *job_meta, double origin_lat,
                                      double origin_lon);

#endif /* GEOMARK_MEASURE_POINTS_EXPORT_H */