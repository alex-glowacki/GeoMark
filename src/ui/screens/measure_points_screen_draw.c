/**
 * @file ui/screens/measure_points_screen_draw.c
 * @brief Measure Points rendering: title bar, left map panel (white
 *        background, captured points plotted as black "X" markers),
 *        right status/input column (fix badge, Point name / Code /
 *        Target height fields, Capture Point button, compact live-fix
 *        readout -- all rendered generically via ui_grid_render() for
 *        the text fields/button, by hand here for the badge/readout),
 *        and the on-screen keyboard filling the bottom half (also
 *        rendered generically via ui_grid_render(), since keyboard
 *        keys are plain WIDGET_BUTTON entries in the same grid -- see
 *        ui/core/keyboard.h's file-level doc comment).
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
 * status/input column and the dark keyboard below it).
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"
#include "ui/tft/display.h"
#include "util/units.h"

#include <stdio.h>
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

#define FIELD_LABEL_X  (STATUS_PANEL_X + MP_FIELD_MARGIN)

/* Compact live-fix readout -- small text, two rows, see this screen's
 * field-crew-decided priority order (badge/inputs/Capture all take
 * priority over this readout's size). */
#define READOUT_ROW1_Y MP_READOUT_Y
#define READOUT_ROW2_Y (MP_READOUT_Y + 16)

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
 * keyboard by design.
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
 * Status/input column -- badge + compact live-fix readout. The three
 * text fields and the Capture Point button are NOT drawn here -- they
 * are real WIDGET_TEXT_FIELD/WIDGET_BUTTON entries in ctx->grid,
 * rendered generically by the ui_grid_render() call at the end of
 * measure_points_screen_render() below, same as every other field on
 * every other screen in this codebase.
 * ---------------------------------------------------------------------- */

static void draw_status_column(const MeasurePointsScreenCtx *ctx)
{
    char buf[56];

    /* Divider between map and status/input column, spanning the full
     * height down to the keyboard boundary. */
    display_fill_rect(STATUS_PANEL_X - 2, PANEL_TOP_Y, 2,
                      (uint16_t)(PANEL_BOTTOM_Y - PANEL_TOP_Y), PANEL_DIVIDER_COL);

    const RtkFeedPosition *pos = &ctx->latest;
    gm_fix_type_t ft = pos->valid ? (gm_fix_type_t)pos->fix_quality : FIX_NONE;

    /* Fix badge */
    uint16_t bw = (uint16_t)(STATUS_PANEL_W - 16);
    display_fill_rect(STATUS_PANEL_X + 8, MP_BADGE_Y, bw, MP_BADGE_H, badge_color(ft));
    display_draw_string(STATUS_PANEL_X + 14, (uint16_t)(MP_BADGE_Y + 6), badge_label(ft),
                        TFT_BLACK, badge_color(ft), 1);

    /* Compact live-fix readout -- small text, two rows. Lowest
     * priority in this column's vertical budget (see
     * measure_points_screen.h's row-math doc comment), so it shrinks
     * first: a single combined line per row rather than the larger
     * separate labeled rows the original (pre-keyboard) layout had. */
    if (!pos->valid) {
        display_draw_string(FIELD_LABEL_X, READOUT_ROW1_Y, "Waiting for fix...",
                            TFT_GRAY, TFT_BLACK, 1);
    } else {
        snprintf(buf, sizeof(buf), "%.6f, %.6f", pos->lat, pos->lon);
        display_draw_string(FIELD_LABEL_X, READOUT_ROW1_Y, buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "Alt %.1fft  HDOP %.1f  Sats %02u",
                gm_m_to_intl_ft(pos->alt), pos->hdop, (unsigned)pos->num_sats);
        display_draw_string(FIELD_LABEL_X, READOUT_ROW2_Y, buf, TFT_WHITE, TFT_BLACK, 1);
    }

    snprintf(buf, sizeof(buf), "Points captured: %u", ctx->points.count);
    display_draw_string(FIELD_LABEL_X, (uint16_t)(READOUT_ROW2_Y + 16), buf,
                        TFT_CYAN, TFT_BLACK, 1);
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

    uint16_t map_x = 0;
    uint16_t map_y = PANEL_TOP_Y;
    uint16_t map_w = (uint16_t)(STATUS_PANEL_X - 4);
    uint16_t map_h = (uint16_t)(PANEL_BOTTOM_Y - PANEL_TOP_Y);
    draw_map_panel(ctx, map_x, map_y, map_w, map_h);

    draw_status_column(ctx);

    /* Renders the three text fields, the Capture Point button, AND the
     * on-screen keyboard's keys -- all real widgets in ctx->grid (see
     * this file's own doc comment). */
    ui_grid_render(&ctx->grid);
}