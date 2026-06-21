/**
 * @file ui/screens/measure_points_screen.c
 * @brief Measure Points screen logic. No ui/tft/display.h dependency
 *        (other than TFT_WIDTH/TFT_HEIGHT, plain constants, same
 *        convention every other screen in this codebase already uses)
 *        -- stays unit-testable on host.
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui/tft/display.h" /* TFT_WIDTH/TFT_HEIGHT only */
#include "util/log.h"

/* -------------------------------------------------------------------------
 * Layout constants shared with measure_points_screen_draw.c live in
 * measure_points_screen.h (STATUS_PANEL_W/_X, PANEL_TOP_Y/_BOTTOM_Y,
 * CAPTURE_BTN_*) -- see that header for why.
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * RTK feed
 * ---------------------------------------------------------------------- */

static void no_feed_fn(void *user, RtkFeedPosition *out)
{
    (void)user;
    memset(out, 0, sizeof(*out));
    out->valid = false;
}

RtkFeed measure_points_no_feed(void)
{
    RtkFeed f = { .fn = no_feed_fn, .user = NULL };
    return f;
}

/* -------------------------------------------------------------------------
 * Capture
 * ---------------------------------------------------------------------- */

static void on_capture_point(UiWidget *self, void *screen_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    (void)self;

    if (!ctx->latest.valid) {
        ctx->status = MEASURE_POINTS_STATUS_NO_FIX;
        return;
    }

    MeasurePoint pt;
    memset(&pt, 0, sizeof(pt));
    pt.lat         = ctx->latest.lat;
    pt.lon         = ctx->latest.lon;
    pt.alt         = ctx->latest.alt;
    pt.fix_quality = ctx->latest.fix_quality;
    pt.hdop        = ctx->latest.hdop;
    pt.num_sats    = ctx->latest.num_sats;
    pt.timestamp   = time(NULL);
    /* code intentionally left empty -- point codes are not yet wired
     * into this screen's UI (no text entry here, see header doc
     * comment); future work, not this session's scope. */

    gm_status_t rc = measure_points_add(&ctx->points, pt);
    if (rc != GM_OK) {
        ctx->status = MEASURE_POINTS_STATUS_STORE_FULL;
        return;
    }

    /* The just-added copy has point_num assigned by measure_points_add();
     * re-read it from the store rather than the local pt, which still
     * has point_num == 0. */
    const MeasurePoint *stored = &ctx->points.points[ctx->points.count - 1];

    if (!ctx->have_origin) {
        ctx->origin_lat  = stored->lat;
        ctx->origin_lon  = stored->lon;
        ctx->have_origin = true;
    }

    if (ctx->job_ctx && ctx->job_ctx->job_dir[0] != '\0') {
        char csv_path[600];
        measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
        measure_points_append_csv(csv_path, stored);
    }

    ctx->status = MEASURE_POINTS_STATUS_NONE;
    log_info("measure_points: captured point #%u", stored->point_num);
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void measure_points_screen_init(MeasurePointsScreenCtx *ctx, UiScreenStack *stack,
                                const JobContext *job_ctx, RtkFeed feed)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack   = stack;
    ctx->job_ctx = job_ctx;
    ctx->feed    = feed;

    job_metadata_defaults(&ctx->job_meta);
    measure_points_init(&ctx->points);

    ui_grid_init(&ctx->grid, ctx);

    UiRect capture_r = { STATUS_PANEL_X + CAPTURE_BTN_MARGIN, CAPTURE_BTN_Y,
                         CAPTURE_BTN_W, CAPTURE_BTN_H };
    ui_grid_add_button(&ctx->grid, capture_r, "Capture Point", on_capture_point);
}

/**
 * Reloads this job's metadata and previously-captured points from disk.
 * Called from on_enter (not init) -- same "read the real on-disk state
 * every time this screen becomes visible, not just once at construction"
 * stance open_job_screen.c's rebuild_job_list() already established,
 * since the active job (via JobContext) can change between visits to
 * this screen (a different job opened via Open Existing Job, etc.).
 */
static void reload_job_data(MeasurePointsScreenCtx *ctx)
{
    ctx->status      = MEASURE_POINTS_STATUS_NONE;
    ctx->have_origin = false;
    measure_points_init(&ctx->points);
    job_metadata_defaults(&ctx->job_meta);

    if (!ctx->job_ctx || !job_context_has_job(ctx->job_ctx)) {
        ctx->status = MEASURE_POINTS_STATUS_NO_JOB;
        return;
    }

    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", ctx->job_ctx->job_dir);
    job_metadata_load(ini_path, &ctx->job_meta); /* missing file -> defaults, not an error */

    char csv_path[600];
    measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
    gm_status_t rc = measure_points_load_csv(csv_path, &ctx->points);
    if (rc != GM_OK) {
        ctx->status = MEASURE_POINTS_STATUS_LOAD_ERROR;
        return;
    }

    if (ctx->points.count > 0) {
        ctx->origin_lat  = ctx->points.points[0].lat;
        ctx->origin_lon  = ctx->points.points[0].lon;
        ctx->have_origin = true;
    }
}

static void measure_points_on_enter(void *raw_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;
    reload_job_data(ctx);
    ui_grid_focus_first(&ctx->grid);
}

static void measure_points_on_tick(void *raw_ctx, uint32_t now_ms)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;
    (void)now_ms;

    if (ctx->feed.fn)
        ctx->feed.fn(ctx->feed.user, &ctx->latest);

    if (ctx->latest.valid && !ctx->have_origin) {
        ctx->origin_lat  = ctx->latest.lat;
        ctx->origin_lon  = ctx->latest.lon;
        ctx->have_origin = true;
    }
}

static bool measure_points_on_event(void *raw_ctx, UiEvent ev)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Job Create/Open Job */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen measure_points_screen_as_ui_screen(MeasurePointsScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = measure_points_on_enter;
    s.on_event  = measure_points_on_event;
    s.on_tick   = measure_points_on_tick;
    s.on_render = measure_points_screen_render;
    s.ctx       = ctx;
    return s;
}