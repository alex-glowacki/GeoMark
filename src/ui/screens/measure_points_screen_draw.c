/**
 * @file ui/screens/measure_points_screen_draw.c
 * @brief Measure Points rendering: title bar, left map panel (white
 *        background, captured points plotted as black "X" markers),
 *        right status/input column (fix badge, titled Point name /
 *        Code / Target height fields, keyboard-toggle button, Capture
 *        Point, compact live-fix readout), and -- when an overlay is
 *        open -- a solid backdrop covering the bottom of the panel
 *        with either the on-screen keyboard or the code-picker list
 *        rendered on top of it.
 *
 * The map panel and status/input column are FIXED at full panel height
 * (PANEL_TOP_Y..PANEL_BOTTOM_Y) regardless of overlay state -- they are
 * drawn first, every frame, exactly the same whether or not an overlay
 * is showing. The overlay backdrop is then painted over the bottom
 * portion (MP_OVERLAY_TOP_Y..TFT_HEIGHT) ON TOP of that, covering
 * whatever was just drawn there; ui_grid_render() afterward draws
 * whichever widgets are actually in the grid (base fields only when no
 * overlay, or base fields + keyboard keys / code-picker buttons when
 * one is open -- see measure_points_screen.c's rebuild_grid()), so the
 * overlay's own widgets land on top of the backdrop in the same call.
 *
 * Fix badge color conventions deliberately mirror ui/tft/screen.c's
 * badge_color()/badge_label() exactly (same colors, same five-way
 * switch) -- this is the legacy production status screen's own
 * established visual language for fix quality; Measure Points should
 * look like the same product, not invent a second one. screen.c itself
 * is untouched (per the production-flow rule) -- this file only reuses
 * its color choices, not its code.
 *
 * Map panel colors: white background, black markers/text (explicit
 * design decision -- distinguishes the map visually from the dark
 * status/input column and the dark overlay backdrop).
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"
#include "ui/tft/display.h"
#include "util/units.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Layout -- mirrors the *_X/_Y constant style every other screen draw
 * file in this codebase already uses (job_create_screen_draw.c,
 * open_job_screen_draw.c, etc.). STATUS_PANEL_W/_X, PANEL_TOP_Y/
 * _BOTTOM_Y, and the MP_* field-row constants come from
 * measure_points_screen.h (shared with that file's widget placement);
 * everything below is local to this draw file only.
 * ---------------------------------------------------------------------- */

#define TITLE_Y   8
#define STATUS_MSG_Y (TITLE_Y + 22)

#define MAP_BORDER_COL   TFT_DKGRAY
#define MAP_BG_COL       TFT_WHITE
#define MAP_POINT_COL    TFT_BLACK
#define MAP_MARGIN       4

#define PANEL_DIVIDER_COL TFT_DKGRAY
#define OVERLAY_BG_COL    TFT_BLACK /* matches UI_COL_BG in widget_draw.c --
                                     * the keyboard/button widgets drawn on
                                     * top already assume this backdrop */

#define FIELD_LABEL_X  (STATUS_PANEL_X + MP_FIELD_MARGIN)

/* -------------------------------------------------------------------------
 * Fix badge -- mirrors ui/tft/screen.c's badge_color()/badge_label().
 * ---------------------------------------------------------------------- */

static uint16_t badge_color(gm_fix_type_t ft)
{
    switch (ft) {
    case FIX_RTK_FIXED: return TFT_GREEN;
    case FIX_RTK_FLOAT: return TFT_CYAN;
    case FIX_DGPS:       return TFT_YELLOW;
    case FIX_SINGLE:     return TFT_ORANGE;
    default:             return TFT_RED;
    }
}

static const char *badge_label(gm_fix_type_t ft)
{
    switch (ft) {
    case FIX_RTK_FIXED: return "RTK FIXED";
    case FIX_RTK_FLOAT: return "RTK FLOAT";
    case FIX_DGPS:       return "DGPS";
    case FIX_SINGLE:     return "SINGLE";
    default:             return "NO FIX";
    }
}

/* -------------------------------------------------------------------------
 * Status text (job-level errors -- no job active, load failed, etc.)
 * ---------------------------------------------------------------------- */

static const char *status_text(MeasurePointsStatus status)
{
    switch (status) {
    case MEASURE_POINTS_STATUS_NO_JOB:
        return "No active job -- start from Job Setup first";
    case MEASURE_POINTS_STATUS_LOAD_ERROR:
        return "Could not load this job's points -- check storage";
    case MEASURE_POINTS_STATUS_STORE_FULL:
        return "Point limit reached for this job";
    case MEASURE_POINTS_STATUS_NO_FIX:
        return "No valid fix -- cannot capture a point yet";
    case MEASURE_POINTS_STATUS_NONE:
    default:
        return NULL;
    }
}

/* -------------------------------------------------------------------------
 * Map panel
 *
 * Plots every captured point as a black "X" via the built-in font's
 * own 'X' glyph -- no line/circle primitive exists in display.h
 * (pixel/rect/string only, see that header), and a glyph is the
 * simplest marker that needs none. Each point is projected through
 * measure_points_project() (job coord_sys aware, see that function's
 * own doc comment) into the job's east/north units, then scaled to fit
 * the panel with a margin. Breakline rendering for coded points is
 * deliberately not implemented here (see measure_points.h's file-level
 * doc comment) -- every point renders as an unconnected "X" today.
 * Background is white, markers/any future map text are black -- the
 * map panel is visually distinct from the dark status/input column and
 * overlay backdrop by design. This panel is FIXED full-height and
 * never shrinks for the overlay (see this file's top-of-file comment).
 * ---------------------------------------------------------------------- */

static void draw_map_panel(const MeasurePointsScreenCtx *ctx, uint16_t map_x, uint16_t map_y,
                           uint16_t map_w, uint16_t map_h)
{
    display_fill_rect(map_x, map_y, map_w, map_h, MAP_BG_COL);
    /* Border -- four thin filled rects, the only line-drawing tool
     * display.h actually offers (no draw_line()). */
    display_fill_rect(map_x, map_y, map_w, 2, MAP_BORDER_COL);
    display_fill_rect(map_x, (uint16_t)(map_y + map_h - 2), map_w, 2, MAP_BORDER_COL);
    display_fill_rect(map_x, map_y, 2, map_h, MAP_BORDER_COL);
    display_fill_rect((uint16_t)(map_x + map_w - 2), map_y, 2, map_h, MAP_BORDER_COL);

    if (!ctx->have_origin || ctx->points.count == 0)
        return;

    uint16_t inner_x = (uint16_t)(map_x + MAP_MARGIN);
    uint16_t inner_y = (uint16_t)(map_y + MAP_MARGIN);
    uint16_t inner_w = (uint16_t)(map_w - 2 * MAP_MARGIN);
    uint16_t inner_h = (uint16_t)(map_h - 2 * MAP_MARGIN);

    /* First pass: find the projected bounding box so every point can be
     * scaled to fit, regardless of the job's coordinate system or
     * units (feet for ND North, meters for the local fallback -- this
     * scaling step is unit-agnostic, it only cares about relative
     * spread). */
    double min_e = 0, max_e = 0, min_n = 0, max_n = 0;
    bool first = true;

    for (uint32_t i = 0; i < ctx->points.count; i++) {
        MeasurePointsProjected p;
        gm_status_t rc = measure_points_project(&ctx->job_meta,
                                                 ctx->points.points[i].lat,
                                                 ctx->points.points[i].lon,
                                                 ctx->origin_lat, ctx->origin_lon, &p);
        if (rc != GM_OK)
            continue;

        if (first) {
            min_e = max_e = p.east;
            min_n = max_n = p.north;
            first = false;
        } else {
            if (p.east < min_e) min_e = p.east;
            if (p.east > max_e) max_e = p.east;
            if (p.north < min_n) min_n = p.north;
            if (p.north > max_n) max_n = p.north;
        }
    }

    if (first)
        return; /* every projection failed (e.g. NaN input) -- nothing to plot */

    double span_e = max_e - min_e;
    double span_n = max_n - min_n;
    /* A single point (or all points coincident) has zero span -- plot
     * it centered rather than dividing by zero. */
    double scale_e = (span_e > 1e-6) ? (double)inner_w / span_e : 0.0;
    double scale_n = (span_n > 1e-6) ? (double)inner_h / span_n : 0.0;
    double scale = (scale_e > 0 && scale_n > 0) ? (scale_e < scale_n ? scale_e : scale_n)
                                                 : 0.0;
    /* Leave a little breathing room rather than touching the border exactly. */
    scale *= 0.9;

    double center_e = (min_e + max_e) / 2.0;
    double center_n = (min_n + max_n) / 2.0;

    for (uint32_t i = 0; i < ctx->points.count; i++) {
        MeasurePointsProjected p;
        gm_status_t rc = measure_points_project(&ctx->job_meta,
                                                 ctx->points.points[i].lat,
                                                 ctx->points.points[i].lon,
                                                 ctx->origin_lat, ctx->origin_lon, &p);
        if (rc != GM_OK)
            continue;

        double dx = (p.east - center_e) * scale;
        /* Screen Y grows downward; map north should grow upward. */
        double dy = -(p.north - center_n) * scale;

        int32_t px = (int32_t)(inner_x + inner_w / 2 + dx);
        int32_t py = (int32_t)(inner_y + inner_h / 2 + dy);

        /* Clip to the inner panel -- a point outside the computed
         * bounding box should not happen (it defines the box), but a
         * defensive clip costs nothing and prevents any stray
         * off-panel write if scale/rounding ever disagrees at an edge. */
        if (px < inner_x || px >= inner_x + inner_w || py < inner_y || py >= inner_y + inner_h)
            continue;

        display_draw_char((uint16_t)px, (uint16_t)py, 'X', MAP_POINT_COL, MAP_BG_COL, 2);
    }
}

/* -------------------------------------------------------------------------
 * Status/input column -- badge, field title labels, and the compact
 * live-fix readout. The text fields themselves, the Pick/Keyboard
 * toggle/Capture Point buttons, and any overlay's widgets are NOT drawn
 * here -- they are real grid widgets, rendered generically by the
 * ui_grid_render() call at the end of measure_points_screen_render()
 * below, same as every other field on every other screen in this
 * codebase. This is FIXED full-height and drawn identically regardless
 * of overlay state (see this file's top-of-file comment).
 * ---------------------------------------------------------------------- */

static void draw_field_label(uint16_t y, const char *text)
{
    display_draw_string(FIELD_LABEL_X, y, text, TFT_GRAY, TFT_BLACK, 1);
}

static void draw_status_column(const MeasurePointsScreenCtx *ctx)
{
    char buf[56];

    /* Divider between map and status/input column, spanning the full
     * panel height. */
    display_fill_rect(STATUS_PANEL_X - 2, PANEL_TOP_Y, 2,
                      (uint16_t)(PANEL_BOTTOM_Y - PANEL_TOP_Y), PANEL_DIVIDER_COL);

    const RtkFeedPosition *pos = &ctx->latest;
    gm_fix_type_t ft = pos->valid ? (gm_fix_type_t)pos->fix_quality : FIX_NONE;

    /* Fix badge */
    uint16_t bw = (uint16_t)(STATUS_PANEL_W - 16);
    display_fill_rect(STATUS_PANEL_X + 8, MP_BADGE_Y, bw, MP_BADGE_H, badge_color(ft));
    display_draw_string(STATUS_PANEL_X + 14, (uint16_t)(MP_BADGE_Y + 7), badge_label(ft),
                        TFT_BLACK, badge_color(ft), 1);

    /* Field title labels -- same "label drawn above the field" pattern
     * job_create_screen.c's WIDGET_LABEL rows use, except these are
     * hand-drawn strings rather than grid WIDGET_LABEL entries: the
     * grid here gets rebuilt per overlay state (rebuild_grid()), and a
     * static title that never changes regardless of overlay has no
     * reason to be a widget that gets torn down and re-added on every
     * rebuild. */
    draw_field_label(MP_NAME_LABEL_Y, "Point name:");
    draw_field_label(MP_CODE_LABEL_Y, "Code:");
    draw_field_label(MP_HEIGHT_LABEL_Y, "Target height (ft):");

    /* Live-fix readout, below Capture Point. MP_READOUT_ROWS rows at
     * MP_READOUT_ROW_H each (measure_points_screen.h) -- Lat, Lon
     * (both DMS-with-bearing, e.g. "97d1'58.44\"W"), corrected
     * elevation (raw altitude minus the typed Target height, the same
     * correction on_capture_point() applies when a point is actually
     * captured -- see units.h's gm_format_dms_bearing() for the format
     * and measure_points_screen.c's on_capture_point() for the
     * correction math this mirrors), HDOP/sats, HRMS/VRMS, and points
     * captured. */
    uint16_t readout_row[MP_READOUT_ROWS];
    for (int i = 0; i < MP_READOUT_ROWS; i++)
        readout_row[i] = (uint16_t)(MP_READOUT_Y + i * MP_READOUT_ROW_H);

    if (!pos->valid) {
        display_draw_string(FIELD_LABEL_X, readout_row[0], "Waiting for fix...",
                            TFT_GRAY, TFT_BLACK, 1);
    } else {
        char dms_buf[32];

        gm_format_dms_bearing(pos->lat, true, dms_buf, sizeof(dms_buf));
        snprintf(buf, sizeof(buf), "Lat  %s", dms_buf);
        display_draw_string(FIELD_LABEL_X, readout_row[0], buf, TFT_WHITE, TFT_BLACK, 1);

        gm_format_dms_bearing(pos->lon, false, dms_buf, sizeof(dms_buf));
        snprintf(buf, sizeof(buf), "Lon  %s", dms_buf);
        display_draw_string(FIELD_LABEL_X, readout_row[1], buf, TFT_WHITE, TFT_BLACK, 1);

        /* Corrected elevation: the typed Target height field is parsed
         * the same way on_capture_point() parses it at capture time
         * (atof() on height_buf, feet -> meters via gm_intl_ft_to_m()),
         * so this preview always matches what an actual capture right
         * now would record -- it is not a separate/independent
         * calculation that could silently drift from the real one. */
        double target_height_ft = atof(ctx->height_buf);
        double target_height_m  = gm_intl_ft_to_m(target_height_ft);
        double corrected_alt_m  = pos->alt - target_height_m;
        snprintf(buf, sizeof(buf), "Elev %.2fft (raw %.2fft)",
                gm_m_to_intl_ft(corrected_alt_m), gm_m_to_intl_ft(pos->alt));
        display_draw_string(FIELD_LABEL_X, readout_row[2], buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "HDOP %.1f  Sats %02u", pos->hdop, (unsigned)pos->num_sats);
        display_draw_string(FIELD_LABEL_X, readout_row[3], buf, TFT_WHITE, TFT_BLACK, 1);

        /* HRMS/VRMS: not yet available. This feed only carries HDOP
         * (from the UM980's $GGA sentence) -- true horizontal/vertical
         * RMS precision figures require parsing $GST (GPS Pseudorange
         * Noise Statistics), which gnss/nmea.c does not implement yet.
         * Showing "N/A" here is an honest placeholder rather than
         * mislabeling HDOP as RMS (a real, different quantity) or
         * silently omitting the row -- the layout reserves this row now
         * so wiring real $GST-derived values in later is a pure data
         * change, no layout rework. */
        display_draw_string(FIELD_LABEL_X, readout_row[4], "HRMS N/A  VRMS N/A",
                            TFT_GRAY, TFT_BLACK, 1);
    }

    snprintf(buf, sizeof(buf), "Points captured: %u", ctx->points.count);
    display_draw_string(FIELD_LABEL_X, readout_row[5], buf, TFT_CYAN, TFT_BLACK, 1);
}

/* -------------------------------------------------------------------------
 * Overlay backdrop -- a solid fill covering the bottom of the panel
 * (MP_OVERLAY_TOP_Y to TFT_HEIGHT, full width) when either overlay is
 * showing, painted AFTER the fixed map/status layout above so it
 * cleanly covers whatever was drawn there. Matches UI_COL_BG
 * (widget_draw.c) exactly so the keyboard's own button widgets (which
 * paint their own dark-gray rects, but rely on a matching black
 * backdrop in the gaps between keys) look identical to how they
 * already render on job_create_screen.c.
 * ---------------------------------------------------------------------- */

static void draw_overlay_backdrop(void)
{
    display_fill_rect(0, MP_OVERLAY_TOP_Y, TFT_WIDTH,
                      (uint16_t)(TFT_HEIGHT - MP_OVERLAY_TOP_Y), OVERLAY_BG_COL);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

void measure_points_screen_render(void *raw_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Measure Points";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, TITLE_Y, title, TFT_CYAN, TFT_BLACK, scale);

    const char *msg = status_text(ctx->status);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        display_draw_string(mx, STATUS_MSG_Y, msg, TFT_ORANGE, TFT_BLACK, 1);
    }

    /* Fixed layout -- always drawn at full panel height, regardless of
     * overlay state (see this file's top-of-file comment). */
    uint16_t map_x = 0;
    uint16_t map_y = PANEL_TOP_Y;
    uint16_t map_w = (uint16_t)(STATUS_PANEL_X - 4);
    uint16_t map_h = (uint16_t)(PANEL_BOTTOM_Y - PANEL_TOP_Y);
    draw_map_panel(ctx, map_x, map_y, map_w, map_h);

    draw_status_column(ctx);

    /* Overlay backdrop, painted on top of the fixed layout, only when
     * an overlay is actually open. */
    if (ctx->overlay != MEASURE_POINTS_OVERLAY_NONE)
        draw_overlay_backdrop();

    /* Renders whichever widgets are currently in ctx->grid -- base
     * fields/buttons only when no overlay is open, or base fields plus
     * the keyboard's keys / code-picker's buttons when one is (see
     * measure_points_screen.c's rebuild_grid()). Drawn last so overlay
     * widgets land on top of the backdrop just painted above. */
    ui_grid_render(&ctx->grid);
}