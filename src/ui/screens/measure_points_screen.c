/**
 * @file ui/screens/measure_points_screen.c
 * @brief Measure Points screen logic. No ui/tft/display.h dependency
 *        (other than through ui/core/keyboard.h's KEYBOARD_TOP_Y/HEIGHT,
 *        plain constants, not function calls -- same convention
 *        job_create_screen.c already uses) -- stays unit-testable on
 *        host.
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util/log.h"
#include "util/units.h"

/* -------------------------------------------------------------------------
 * Layout constants shared with measure_points_screen_draw.c live in
 * measure_points_screen.h (STATUS_PANEL_W/_X, PANEL_TOP_Y/_BOTTOM_Y,
 * MP_*) -- see that header for why.
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
 * Keyboard field switching -- same pattern job_create_screen.c's
 * set_active_field() already established for its six fields, here for
 * this screen's three (Point name, Code, Target height).
 * ---------------------------------------------------------------------- */

static void set_active_field(MeasurePointsScreenCtx *ctx, MeasurePointsActiveField field)
{
    ctx->active_field = field;

    switch (field) {
    case MEASURE_POINTS_FIELD_NAME:
        ctx->kb.buf     = ctx->name_buf;
        ctx->kb.buf_cap = sizeof(ctx->name_buf);
        ctx->kb.len     = &ctx->name_len;
        break;
    case MEASURE_POINTS_FIELD_CODE:
        ctx->kb.buf     = ctx->code_buf;
        ctx->kb.buf_cap = sizeof(ctx->code_buf);
        ctx->kb.len     = &ctx->code_len;
        break;
    case MEASURE_POINTS_FIELD_HEIGHT:
        ctx->kb.buf     = ctx->height_buf;
        ctx->kb.buf_cap = sizeof(ctx->height_buf);
        ctx->kb.len     = &ctx->height_len;
        break;
    case MEASURE_POINTS_FIELD_NONE:
    default:
        ctx->kb.buf = NULL;
        ctx->kb.len = NULL;
        break;
    }
}

static void on_name_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((MeasurePointsScreenCtx *)screen_ctx, MEASURE_POINTS_FIELD_NAME);
}
static void on_code_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((MeasurePointsScreenCtx *)screen_ctx, MEASURE_POINTS_FIELD_CODE);
}
static void on_height_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((MeasurePointsScreenCtx *)screen_ctx, MEASURE_POINTS_FIELD_HEIGHT);
}

static void on_keyboard_done(void *screen_ctx)
{
    /* Same convention as Job Create / New Project: Done just drops
     * keyboard focus, it does not submit. */
    (void)screen_ctx;
}

/* -------------------------------------------------------------------------
 * Point-name auto-increment
 *
 * "Purely numeric" means strtol() consumes the ENTIRE string (endptr
 * lands exactly on the trailing NUL) and the string is non-empty --
 * the standard idiom for "is this whole string an integer", not just
 * "does it start with one" (strtol() alone would happily accept
 * "12abc" and silently ignore "abc", which is not what "purely
 * numeric" should mean here).
 * ---------------------------------------------------------------------- */

static bool parse_purely_numeric(const char *s, long *out)
{
    if (!s || s[0] == '\0')
        return false;

    char *endptr = NULL;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || *endptr != '\0')
        return false;

    *out = v;
    return true;
}

static void advance_name_if_numeric(MeasurePointsScreenCtx *ctx)
{
    long v;
    if (!parse_purely_numeric(ctx->name_buf, &v))
        return; /* non-numeric name -- left untouched, see header doc comment */

    snprintf(ctx->name_buf, sizeof(ctx->name_buf), "%ld", v + 1);
    ctx->name_len = strlen(ctx->name_buf);
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

    /* Target height: typed in feet (see measure_points_screen.h's
     * file-level doc comment for why), parsed via atof() (same
     * string-to-double convention gnss/nmea.c already uses), converted
     * to meters for internal storage. An empty/unparseable height_buf
     * yields 0.0 ft -- atof()'s own documented behavior on a string
     * with no valid number, which is exactly "no correction applied",
     * a sane default for a field crew that hasn't entered one yet. */
    double target_height_ft = atof(ctx->height_buf);
    double target_height_m  = gm_intl_ft_to_m(target_height_ft);

    MeasurePoint pt;
    memset(&pt, 0, sizeof(pt));
    pt.lat             = ctx->latest.lat;
    pt.lon             = ctx->latest.lon;
    pt.raw_alt         = ctx->latest.alt;
    pt.target_height_m = target_height_m;
    pt.alt             = pt.raw_alt - pt.target_height_m; /* corrected ground elevation */
    pt.fix_quality     = ctx->latest.fix_quality;
    pt.hdop            = ctx->latest.hdop;
    pt.num_sats        = ctx->latest.num_sats;
    pt.timestamp       = time(NULL);
    strncpy(pt.name, ctx->name_buf, sizeof(pt.name) - 1);
    strncpy(pt.code, ctx->code_buf, sizeof(pt.code) - 1);

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

    advance_name_if_numeric(ctx);

    ctx->status = MEASURE_POINTS_STATUS_NONE;
    log_info("measure_points: captured point #%u ('%s')", stored->point_num, stored->name);
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

    /* Point names start at "1" -- the first shot of a brand-new screen
     * instance is purely-numeric by default, so auto-increment applies
     * immediately without the crew having to type anything first. */
    strncpy(ctx->name_buf, "1", sizeof(ctx->name_buf) - 1);
    ctx->name_len = strlen(ctx->name_buf);

    ctx->kb.on_done    = on_keyboard_done;
    ctx->kb.screen_ctx = ctx;

    ui_grid_init(&ctx->grid, ctx);

    UiRect name_r   = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_NAME_Y,   MP_FIELD_W, MP_FIELD_H };
    UiRect code_r   = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_CODE_Y,   MP_FIELD_W, MP_FIELD_H };
    UiRect height_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_HEIGHT_Y, MP_FIELD_W, MP_FIELD_H };
    UiRect capture_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_CAPTURE_Y,
                         MP_FIELD_W, MP_CAPTURE_H };

    UiWidget *name_w = ui_grid_add_text_field(&ctx->grid, name_r, "Point name",
                                              ctx->name_buf, sizeof(ctx->name_buf));
    if (name_w) name_w->on_activate = on_name_activate;

    UiWidget *code_w = ui_grid_add_text_field(&ctx->grid, code_r, "Code",
                                              ctx->code_buf, sizeof(ctx->code_buf));
    if (code_w) code_w->on_activate = on_code_activate;

    UiWidget *height_w = ui_grid_add_text_field(&ctx->grid, height_r, "Target height",
                                                ctx->height_buf, sizeof(ctx->height_buf));
    if (height_w) height_w->on_activate = on_height_activate;

    ui_grid_add_button(&ctx->grid, capture_r, "Capture Point", on_capture_point);

    /* Point name is the first focusable widget added -- start editing
     * it by default, same convention every keyboard-using screen in
     * this codebase already follows (job_create_screen.c, etc.). */
    set_active_field(ctx, MEASURE_POINTS_FIELD_NAME);

    keyboard_add_to_grid(&ctx->grid, &ctx->kb_labels);
}

/**
 * Reloads this job's metadata and previously-captured points from disk.
 * Called from on_enter (not init) -- same "read the real on-disk state
 * every time this screen becomes visible, not just once at construction"
 * stance open_job_screen.c's rebuild_job_list() already established,
 * since the active job (via JobContext) can change between visits to
 * this screen (a different job opened via Open Existing Job, etc.).
 *
 * Deliberately does NOT touch name_buf/code_buf/height_buf -- those are
 * live field-crew input, not job-derived state; re-entering this screen
 * for a different job should not silently erase whatever was mid-typed
 * (the field crew's actual workflow is shoot-shoot-shoot within one
 * job, rarely re-entering this screen for a *different* job with
 * pending unsaved field text).
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