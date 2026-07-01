/**
 * @file staticlog/station.h
 * @brief Static raw-observation logging: sit the UM980 still over a
 *        desired point for an extended occupation, log raw pseudorange/
 *        carrier-phase + ephemeris data to a file, then process that
 *        file OUTSIDE GeoMark into a real, independently-known
 *        coordinate for use with Measure Points' "Localize" feature
 *        (see ui/screens/measure_points_screen.h's file-level
 *        "Localize" doc comment).
 *
 * This is the missing piece that makes the "establish my own property
 * CP once, near a known CORS station, instead of driving to a distant
 * monument every time" workflow possible. GeoMark's own job here ends
 * at "produce a raw log file" -- everything after that is intentionally
 * OUTSIDE GeoMark and outside this codebase, because it depends on
 * vendor tooling this project has no control over:
 *
 *   1. Run this mode (--mode static-log --out <path>) with the UM980
 *      (rover unit, most practically -- it's the one that gets carried
 *      to an arbitrary spot) sitting still over the desired point, for
 *      as long as practical. Longer is better for OPUS solution
 *      quality; a couple of hours is a reasonable target if your
 *      schedule allows it, but even a shorter session may work well
 *      given proximity to a real CORS station (see the session that
 *      established this feature for the NDHILLSBORO CORS example).
 *      Stop with Ctrl-C (SIGINT) or SIGTERM when done.
 *
 *   2. IMPORTANT -- test this on a SHORT (few-minute) capture first,
 *      not directly on a multi-hour occupation. See um980_init_static_
 *      log()'s own doc comment (gnss/um980.h): the exact ephemeris log
 *      command names sent here were confirmed only against Unicore's
 *      command manual's table of contents, not its full body text, and
 *      could be wrong for this specific firmware revision. A wrong
 *      command name fails loudly and immediately (logged with the
 *      exact rejected command) -- much better to discover that in a
 *      5-minute test than after driving home from a 2-hour occupation.
 *
 *   3. Copy the resulting file off the Pi (scp) to a Windows machine
 *      (WSL2's own Windows host works fine for this). Unicore's own
 *      RINEX conversion tool ("Converter", part of their UPrecise
 *      software) is Windows-only -- confirmed during the session that
 *      built this feature, not a GeoMark limitation to work around.
 *      Open the log in UPrecise's Converter, produce RINEX v3.02
 *      observation + navigation files.
 *
 *   4. Submit those RINEX files to NGS OPUS (https://opus.ngs.noaa.gov)
 *      -- standard OPUS for a longer occupation, OPUS-RS (Rapid
 *      Static) if the session was short and you're within OPUS-RS's
 *      supported range of a CORS station. OPUS returns a precise
 *      NAD83(2011) coordinate for wherever you occupied.
 *
 *   5. Read the State Plane Northing/Easting directly off the OPUS
 *      report (OPUS reports SPC for the applicable zone automatically)
 *      plus the NAVD88 orthometric height (GEOID18). Confirm the units:
 *      ND North's SPC zone is legally defined in International Feet
 *      (see collector/coords.c's own EPSG:2265 doc comment) -- if OPUS
 *      reports the SPC values in meters instead, convert using exactly
 *      0.3048 (International Foot), NOT the US Survey Foot factor
 *      (3937/1200), which is a different, incompatible foot definition.
 *      Same 0.3048 conversion for the orthometric height. If the SPC
 *      section isn't present on the report, use NGS's own free NCAT
 *      tool (https://www.ngs.noaa.gov/NCAT/) to convert the reported
 *      lat/lon/height into ND North SPC directly -- do not hand-roll
 *      the projection math. These converted values are what go into
 *      the "known" Northing/Easting/Elevation fields in Measure
 *      Points' Localize overlay the next time you're at that spot.
 *
 * No config file changes needed beyond what base/rover already read
 * (util/config.h's gm_config_t serial_device/serial_baud) -- this mode
 * reuses those same fields for whichever UM980 it's pointed at.
 */

#ifndef GEOMARK_STATICLOG_STATION_H
#define GEOMARK_STATICLOG_STATION_H

#include "geomark.h"

/**
 * Opens the UM980 named by config_path's serial_device/serial_baud,
 * configures it for static raw-observation logging (see
 * um980_init_static_log(), gnss/um980.h), then copies raw bytes from
 * the UM980 straight to out_path (binary, no reinterpretation --
 * GeoMark does not parse or understand this data itself, see this
 * header's file-level doc comment for why) until SIGINT or SIGTERM.
 * Logs a progress line periodically (elapsed time, bytes written) so
 * the operation's liveness can be confirmed over SSH during a long,
 * otherwise-silent occupation.
 *
 * Returns GM_OK after a clean SIGINT/SIGTERM shutdown, GM_ERR_IO if
 * the UM980 or out_path cannot be opened, GM_ERR_GENERIC if
 * um980_init_static_log() rejects one of its commands (already logged
 * with specifics by that function itself).
 */
gm_status_t staticlog_station_run(const char *config_path, const char *out_path);

#endif /* GEOMARK_STATICLOG_STATION_H */