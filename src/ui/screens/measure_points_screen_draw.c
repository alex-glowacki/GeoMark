/**
 * @file ui/screens/measure_points_screen_draw.c
 * @brief Measure Points rendering: title bar, left map panel (white
 *        background, captured points plotted as small filled-square
 *        "dot" markers with their point number labeled below, plus
 *        connecting lines for any breaklines -- see
 *        collector/breaklines.h -- the captured codes form), right
 *        status/input column (fix badge, titled Point name / Code /
 *        Target height fields, keyboard-toggle button, Capture Point,
 *        compact live-fix readout), and -- when an overlay is open -- a
 *        solid backdrop covering the bottom of the panel with either
 *        the on-screen keyboard or the code-picker list rendered on top
 *        of it.
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
 * Map panel colors: white background, black markers/text/lines
 * (explicit design decision -- distinguishes the map visually from the
 * dark status/input column and the dark overlay backdrop).
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"
#include "collector/breaklines.h"
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
#define MAP_LINE_COL     TFT_BLACK /* breakline connecting segments --
                                    * same black as the dot markers and
                                    * their labels; the map panel's
                                    * white background is what visually
                                    * separates a line from the rest of
                                    * the dark UI, not a separate color
                                    * for lines vs. points */
#define MAP_MARGIN       4

/** Dot marker size, in pixels -- a small filled square rather than the
 *  prior "X" glyph (drawn via display_draw_char() at 2x scale, 10x10px
 *  effective footprint). 4x4 is the smallest square that's still
 *  reliably visible/tappable-looking at this panel's resolution, and
 *  costs nothing extra to draw (a single display_fill_rect() call, same
 *  primitive the panel border below already uses) -- no curve math
 *  needed for a "circle", consistent with this module's existing
 *  "rect-only primitives" approach to anything that isn't text. */
#define MAP_DOT_SIZE 4

/** Point-number label, drawn left-justified directly below each dot at
 *  the smallest available text scale (1 = 5x7px glyphs) -- the smallest
 *  legible size display.h's built-in font offers, chosen specifically
 *  so a labeled point doesn't visually dominate the map the way the
 *  prior two-line "Point Number + Code" design would have at a map
 *  panel's typical point density (see this session's own design
 *  discussion for why the Code line was dropped from this label
 *  entirely, not just shrunk). */
#define MAP_LABEL_SCALE 1
#define MAP_LABEL_GAP   1 /* px between the dot's bottom edge and the label's top */

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
 * Thin line segment -- the only line-drawing primitive display.h
 * actually offers is display_fill_rect() (used for the panel border
 * below), which only draws axis-aligned rectangles; breaklines connect
 * arbitrary point pairs, so this is a small integer (Bresenham) line
 * rasterizer built on top of display_draw_pixel(), the one primitive
 * general enough for this. Drawn 1px at a time rather than thickened --
 * at this panel's scale a 1px line is plainly visible against the white
 * map background without needing a second pass to fatten it.
 * ---------------------------------------------------------------------- */

static void draw_line_segment(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t adx = (dx < 0) ? -dx : dx;
    int32_t ady = (dy < 0) ? -dy : dy;
    int32_t sx  = (dx < 0) ? -1 : 1;
    int32_t sy  = (dy < 0) ? -1 : 1;

    int32_t x = x0;
    int32_t y = y0;

    if (adx >= ady) {
        int32_t err = adx / 2;
        for (int32_t i = 0; i <= adx; i++) {
            display_draw_pixel((uint16_t)x, (uint16_t)y, color);
            err -= ady;
            if (err < 0) {
                y += sy;
                err += adx;
            }
            x += sx;
        }
    } else {
        int32_t err = ady / 2;
        for (int32_t i = 0; i <= ady; i++) {
            display_draw_pixel((uint16_t)x, (uint16_t)y, color);
            err -= adx;
            if (err < 0) {
                x += sx;
                err += ady;
            }
            y += sy;
        }
    }
}

/* -------------------------------------------------------------------------
 * Map panel
 *
 * Plots every captured point as a small filled-square "dot" (see
 * MAP_DOT_SIZE), with its point number labeled left-justified directly
 * below it at the smallest available text scale (see MAP_LABEL_SCALE) --
 * deliberately just the number, not the code, to keep a map with many
 * points readable (see MAP_LABEL_SCALE's own doc comment). Each point is
 * projected through measure_points_project() (job coord_sys aware, see
 * that function's own doc comment) into the job's east/north units,
 * then scaled to fit the panel with a margin. Screen positions are
 * computed once into a local per-point array (screen_x/screen_y,
 * screen_valid) so the breakline pass below can look up a vertex's
 * position by its MeasurePointStore index without re-running the
 * projection a second time.
 *
 * Breaklines (see collector/breaklines.h for what these are and the
 * exact algorithm) are drawn as connected line segments BEFORE the dots
 * and labels, so a line never visually occludes the marker/text of any
 * point it passes through -- the dot and its number always render on
 * top of the line that connects it to its neighbors.
 *
 * Background is white, markers/labels/lines are all black -- the map
 * panel is visually distinct from the dark status/input column and
 * overlay backdrop by design. This panel is FIXED full-height and
 * never shrinks for the overlay (see this file's top-of-file comment).
 * ---------------------------------------------------------------------- */

static void draw_map_panel(const MeasurePointsScreenCtx *ctx, uint16_t map_x, uint16_t map_y,
                           uint16_t map_w, uint16_t map_h)
{
    display_fill_rect(map_x, map_y, map_w, map_h, MAP_BG_COL);
    /* Border -- four thin filled rects, the only axis-aligned-rect tool
     * display.h actually offers. */
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

    /* Second pass: resolve every point's screen position once, so the
     * breakline pass below can look up a vertex by store index without
     * re-projecting. GM_MEASURE_POINTS_MAX-sized stack arrays mirror
     * the same fixed-capacity convention MeasurePointStore itself
     * already uses (collector/measure_points.h) -- this function is
     * never reentrant/recursive, so a single stack-resident pair of
     * GM_MEASURE_POINTS_MAX-element arrays here is the same kind of
     * "fixed capacity sized to the documented ceiling" tradeoff, not a
     * new pattern. */
    static int32_t screen_x[GM_MEASURE_POINTS_MAX];
    static int32_t screen_y[GM_MEASURE_POINTS_MAX];
    static bool screen_valid[GM_MEASURE_POINTS_MAX];

    for (uint32_t i = 0; i < ctx->points.count; i++) {
        MeasurePointsProjected p;
        gm_status_t rc = measure_points_project(&ctx->job_meta,
                                                 ctx->points.points[i].lat,
                                                 ctx->points.points[i].lon,
                                                 ctx->origin_lat, ctx->origin_lon, &p);
        if (rc != GM_OK) {
            screen_valid[i] = false;
            continue;
        }

        double dx = (p.east - center_e) * scale;
        /* Screen Y grows downward; map north should grow upward. */
        double dy = -(p.north - center_n) * scale;

        int32_t px = (int32_t)(inner_x + inner_w / 2 + dx);
        int32_t py = (int32_t)(inner_y + inner_h / 2 + dy);

        /* Clip to the inner panel -- a point outside the computed
         * bounding box should not happen (it defines the box), but a
         * defensive clip costs nothing and prevents any stray
         * off-panel write if scale/rounding ever disagrees at an edge. */
        if (px < inner_x || px >= inner_x + inner_w || py < inner_y || py >= inner_y + inner_h) {
            screen_valid[i] = false;
            continue;
        }

        screen_x[i] = px;
        screen_y[i] = py;
        screen_valid[i] = true;
    }

    /* Breaklines first, so dots/labels always render on top of the
     * line connecting them, never under it. A vertex whose own
     * projection failed/clipped (screen_valid[idx] == false) simply
     * breaks that one segment of the line -- the rest of the line still
     * draws normally on either side of the gap. static for the same
     * reason screen_x/screen_y/screen_valid above are static -- this
     * function is never reentrant/recursive (see this file's top-of-
     * file comment: render is one call per frame from the screen
     * stack), so keeping BreaklineSet (see collector/breaklines.h for
     * its sizing) off the stack costs nothing and removes any doubt
     * about stack headroom on this build's actual thread. */
    static BreaklineSet lines;
    breaklines_build(&ctx->points, &lines);

    for (uint32_t li = 0; li < lines.count; li++) {
        const Breakline *line = &lines.lines[li];
        for (uint32_t v = 1; v < line->vertex_count; v++) {
            uint32_t idx_a = line->vertex_indices[v - 1];
            uint32_t idx_b = line->vertex_indices[v];
            if (!screen_valid[idx_a] || !screen_valid[idx_b])
                continue;
            draw_line_segment(screen_x[idx_a], screen_y[idx_a],
                              screen_x[idx_b], screen_y[idx_b], MAP_LINE_COL);
        }
    }

    /* Dots and point-number labels, on top of any line segments just drawn. */
    char num_buf[16];
    for (uint32_t i = 0; i < ctx->points.count; i++) {
        if (!screen_valid[i])
            continue;

        int32_t px = screen_x[i];
        int32_t py = screen_y[i];

        /* Centered on the point's true projected position -- MAP_DOT_SIZE/2
         * offset so the dot's center, not its top-left corner, lands on
         * (px, py) (matches how display_draw_char()'s glyph-cell origin
         * made the prior "X" marker's visual center line up with (px, py)
         * closely enough that no caller-visible position changed when the
         * marker itself did). */
        int32_t dot_x = px - MAP_DOT_SIZE / 2;
        int32_t dot_y = py - MAP_DOT_SIZE / 2;
        display_fill_rect((uint16_t)dot_x, (uint16_t)dot_y, MAP_DOT_SIZE, MAP_DOT_SIZE,
                          MAP_POINT_COL);

        snprintf(num_buf, sizeof(num_buf), "%u", ctx->points.points[i].point_num);
        int32_t label_x = px - MAP_DOT_SIZE / 2; /* left-justified under the dot's left edge */
        int32_t label_y = py + MAP_DOT_SIZE / 2 + MAP_LABEL_GAP;
        display_draw_string((uint16_t)label_x, (uint16_t)label_y, num_buf,
                            MAP_POINT_COL, MAP_BG_COL, MAP_LABEL_SCALE);
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