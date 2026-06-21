/**
 * @file ui/screens/measure_points_screen_draw.c
 * @brief Measure Points rendering: title bar, left map panel (captured
 *        points plotted as "X" markers), right status panel (fix
 *        badge, lat/lon/alt/hdop/sats/age, point count, Capture Point
 *        button via the widget grid).
 *
 * Fix badge color/label conventions deliberately mirror
 * ui/tft/screen.c's badge_color()/badge_label() exactly (same colors,
 * same five-way switch) -- this is the legacy production status
 * screen's own established visual language for fix quality; Measure
 * Points should look like the same product, not invent a second one.
 * screen.c itself is untouched (per the production-flow rule) --this
 * file only reuses its color choices, not its code.
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"
#include "ui/tft/display.h"
#include "util/units.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Layout -- mirrors the *_X/_Y constant style every other screen draw
 * file in this codebase already uses (job_create_screen_draw.c,
 * open_job_screen_draw.c, etc.). STATUS_PANEL_W/_X and PANEL_TOP_Y/
 * _BOTTOM_Y come from measure_points_screen.h (shared with that file's
 * Capture Point button placement); everything below is local to this
 * draw file only.
 * ---------------------------------------------------------------------- */

#define TITLE_Y   8
#define STATUS_MSG_Y (TITLE_Y + 22)

#define MAP_BORDER_COL   TFT_DKGRAY
#define MAP_BG_COL       TFT_BLACK
#define MAP_POINT_COL    TFT_GREEN
#define MAP_MARGIN       4

#define PANEL_DIVIDER_COL TFT_DKGRAY

/* Status panel field layout, relative to STATUS_PANEL_X */
#define BADGE_Y       (PANEL_TOP_Y + 4)
#define BADGE_H       36
#define ROW_GAP       34
#define ROW1_Y        (BADGE_Y + BADGE_H + 16)  /* LAT */
#define ROW2_Y        (ROW1_Y + ROW_GAP)        /* LON */
#define ROW3_Y        (ROW2_Y + ROW_GAP)        /* ALT */
#define ROW4_Y        (ROW3_Y + ROW_GAP)        /* HDOP */
#define ROW5_Y        (ROW4_Y + ROW_GAP)        /* SATS */
#define ROW6_Y        (ROW5_Y + ROW_GAP)        /* AGE */
#define ROW7_Y        (ROW6_Y + ROW_GAP + 8)    /* POINTS captured */

#define FIELD_LABEL_X  (STATUS_PANEL_X + 8)
#define FIELD_VALUE_X  (STATUS_PANEL_X + 8)

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
 * Plots every captured point as an "X" via the built-in font's own 'X'
 * glyph -- no line/circle primitive exists in display.h (pixel/rect/
 * string only, see that header), and a glyph is the simplest marker
 * that needs none. Each point is projected through
 * measure_points_project() (job coord_sys aware, see that function's
 * own doc comment) into the job's east/north units, then scaled to fit
 * the panel with a margin. Breakline rendering for coded points is
 * deliberately not implemented here (see measure_points.h's file-level
 * doc comment) -- every point renders as an unconnected "X" today.
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
 * Status panel
 * ---------------------------------------------------------------------- */

static void draw_status_panel(const MeasurePointsScreenCtx *ctx)
{
    char buf[48];

    /* Divider between map and status panels. */
    display_fill_rect(STATUS_PANEL_X - 2, PANEL_TOP_Y, 2,
                      (uint16_t)(PANEL_BOTTOM_Y - PANEL_TOP_Y), PANEL_DIVIDER_COL);

    const RtkFeedPosition *pos = &ctx->latest;
    gm_fix_type_t ft = pos->valid ? (gm_fix_type_t)pos->fix_quality : FIX_NONE;

    /* Fix badge */
    uint16_t bw = (uint16_t)(STATUS_PANEL_W - 16);
    display_fill_rect(STATUS_PANEL_X + 8, BADGE_Y, bw, BADGE_H, badge_color(ft));
    display_draw_string(STATUS_PANEL_X + 14, (uint16_t)(BADGE_Y + 10), badge_label(ft),
                        TFT_BLACK, badge_color(ft), 2);

    if (!pos->valid) {
        display_draw_string(FIELD_LABEL_X, ROW1_Y, "Waiting for fix...", TFT_GRAY, TFT_BLACK, 1);
    } else {
        snprintf(buf, sizeof(buf), "LAT %.7f", pos->lat);
        display_draw_string(FIELD_LABEL_X, ROW1_Y, buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "LON %.7f", pos->lon);
        display_draw_string(FIELD_LABEL_X, ROW2_Y, buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "ALT %.1f ft", gm_m_to_intl_ft(pos->alt));
        display_draw_string(FIELD_LABEL_X, ROW3_Y, buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "HDOP %.1f", pos->hdop);
        display_draw_string(FIELD_LABEL_X, ROW4_Y, buf, TFT_WHITE, TFT_BLACK, 1);

        snprintf(buf, sizeof(buf), "SATS %02u", (unsigned)pos->num_sats);
        display_draw_string(FIELD_LABEL_X, ROW5_Y, buf, TFT_WHITE, TFT_BLACK, 1);
    }

    snprintf(buf, sizeof(buf), "POINTS CAPTURED: %u", ctx->points.count);
    display_draw_string(FIELD_VALUE_X, ROW7_Y, buf, TFT_CYAN, TFT_BLACK, 1);
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

    draw_status_panel(ctx);

    ui_grid_render(&ctx->grid);
}