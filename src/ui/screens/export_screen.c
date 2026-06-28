/**
 * @file ui/screens/export_screen.c
 * @brief Export screen logic. No ui/tft/display.h dependency (other
 *        than the TFT_WIDTH layout constant, plain macro, no function
 *        call -- same convention job_setup_screen.c already uses).
 */

#define _GNU_SOURCE

#include "ui/screens/export_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "collector/measure_points_export.h"
#include "collector/usb_export.h"
#include "ui/tft/display.h" /* TFT_WIDTH only */
#include "util/log.h"

#define EXPORT_MARGIN 20
#define EXPORT_BTN_H  56
#define EXPORT_GAP    18
#define EXPORT_BTN_W  (TFT_WIDTH - 2 * EXPORT_MARGIN)
#define EXPORT_BTN_TOP_Y 150 /* leaves room above for title + job summary text,
                              * see export_screen_draw.c */

/* -------------------------------------------------------------------------
 * Reload -- same "read the real on-disk state every time this screen
 * becomes visible" stance reload_job_data() (measure_points_screen.c)
 * and rebuild_job_list() (open_job_screen.c) already each established.
 * Called from on_enter, not init -- the active job (via JobContext)
 * can change between visits (a different job opened via Open Existing
 * Job, then Measure Points -> Export again).
 * ---------------------------------------------------------------------- */

static void reload_export_data(ExportScreenCtx *ctx)
{
    measure_points_init(&ctx->points);
    job_metadata_defaults(&ctx->job_meta);

    if (!ctx->job_ctx || !job_context_has_job(ctx->job_ctx)) {
        ctx->status = EXPORT_SCREEN_STATUS_NO_JOB;
        return;
    }

    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", ctx->job_ctx->job_dir);
    job_metadata_load(ini_path, &ctx->job_meta); /* missing file -> defaults, not an error */

    char csv_path[600];
    measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
    gm_status_t rc = measure_points_load_csv(csv_path, &ctx->points);
    if (rc != GM_OK) {
        ctx->status = EXPORT_SCREEN_STATUS_LOAD_ERROR;
        return;
    }

    ctx->status = EXPORT_SCREEN_STATUS_NONE;
}

/**
 * The job's points are stored in WGS84 lat/lon (measure_points.h's
 * MeasurePoint doc comment) -- the local-fallback projection
 * (measure_points_project(), see that function's own doc comment)
 * needs an origin point for every coord_sys except ND North, where
 * the projection is absolute and the origin is ignored regardless
 * (measure_points_export.h's own file-level doc comment). Same "first
 * point in the store sets the origin" convention
 * measure_points_screen.c's reload_job_data() already establishes for
 * the very same store shape -- recomputed here rather than threaded
 * through JobContext, since nothing else on this screen needs an
 * origin and MeasurePointStore already has everything required to
 * derive it.
 */
static void resolve_origin(const MeasurePointStore *points, double *origin_lat,
                           double *origin_lon)
{
    if (points->count > 0) {
        *origin_lat = points->points[0].lat;
        *origin_lon = points->points[0].lon;
    } else {
        *origin_lat = 0.0;
        *origin_lon = 0.0;
    }
}

/* -------------------------------------------------------------------------
 * Destination resolution -- USB drive when actually mounted, internal
 * job_dir/export/ otherwise. One shared resolver rather than a
 * per-format function: usb_export_path_for_job() already computes both
 * the LandXML and CSV destination paths together in a single call (one
 * mkdir sequence for both), so resolving both up front and letting each
 * button callback pick the one it needs is simpler than two near-
 * duplicate functions each independently calling usb_export_path_for_job()
 * and discarding half of what it returns.
 *
 * Checked fresh on every call (not cached anywhere in ExportScreenCtx)
 * -- see export_screen.h's file-level doc comment for why this must
 * reflect the drive's state at the moment of the actual button press,
 * not whenever this screen was last entered.
 *
 * Returns true if the USB drive was used (out_xml_path/out_csv_path are
 * both on the USB drive), false if this fell back to internal storage
 * -- either because the drive was not mounted, or because
 * usb_export_path_for_job() itself failed for any other reason (e.g.
 * an unexpectedly-shaped job_dir). Either way the crew still gets
 * their export, just not on the drive, and the caller is responsible
 * for reporting that as a FALLBACK status rather than a silent
 * success.
 * ---------------------------------------------------------------------- */

static bool resolve_export_destination(const JobContext *job_ctx, char *out_xml_path,
                                       size_t xml_path_len, char *out_csv_path,
                                       size_t csv_path_len)
{
    if (usb_export_is_mounted() &&
        usb_export_path_for_job(job_ctx->job_dir, out_xml_path, xml_path_len, out_csv_path,
                                csv_path_len) == GM_OK)
        return true;

    measure_points_export_landxml_path(job_ctx->job_dir, out_xml_path, xml_path_len);
    measure_points_export_csv_path(job_ctx->job_dir, out_csv_path, csv_path_len);
    return false;
}

/* -------------------------------------------------------------------------
 * Export button callbacks
 * ---------------------------------------------------------------------- */

static void on_export_landxml(UiWidget *self, void *screen_ctx)
{
    (void)self;
    ExportScreenCtx *ctx = (ExportScreenCtx *)screen_ctx;

    if (!ctx->job_ctx || !job_context_has_job(ctx->job_ctx)) {
        ctx->status = EXPORT_SCREEN_STATUS_NO_JOB;
        return;
    }

    double origin_lat, origin_lon;
    resolve_origin(&ctx->points, &origin_lat, &origin_lon);

    char xml_path[USB_EXPORT_PATH_MAX];
    char csv_path[USB_EXPORT_PATH_MAX]; /* unused by this callback, but
                                         * resolve_export_destination()
                                         * always resolves both together --
                                         * see that function's own doc
                                         * comment for why */
    bool used_usb =
        resolve_export_destination(ctx->job_ctx, xml_path, sizeof(xml_path), csv_path,
                                   sizeof(csv_path));

    gm_status_t rc = measure_points_export_landxml(xml_path, &ctx->points, &ctx->job_meta,
                                                   origin_lat, origin_lon);

    if (rc != GM_OK) {
        ctx->status = EXPORT_SCREEN_STATUS_LANDXML_ERROR;
        return;
    }

    ctx->status = used_usb ? EXPORT_SCREEN_STATUS_LANDXML_OK
                           : EXPORT_SCREEN_STATUS_LANDXML_OK_FALLBACK;
    log_info("export_screen: wrote LandXML for job '%s' (%u points) to %s (%s)",
             ctx->job_ctx->name, ctx->points.count, xml_path,
             used_usb ? "USB" : "internal storage");
}

static void on_export_csv(UiWidget *self, void *screen_ctx)
{
    (void)self;
    ExportScreenCtx *ctx = (ExportScreenCtx *)screen_ctx;

    if (!ctx->job_ctx || !job_context_has_job(ctx->job_ctx)) {
        ctx->status = EXPORT_SCREEN_STATUS_NO_JOB;
        return;
    }

    double origin_lat, origin_lon;
    resolve_origin(&ctx->points, &origin_lat, &origin_lon);

    char xml_path[USB_EXPORT_PATH_MAX]; /* unused by this callback, see
                                         * on_export_landxml()'s identical
                                         * comment above */
    char csv_path[USB_EXPORT_PATH_MAX];
    bool used_usb =
        resolve_export_destination(ctx->job_ctx, xml_path, sizeof(xml_path), csv_path,
                                   sizeof(csv_path));

    gm_status_t rc = measure_points_export_csv(csv_path, &ctx->points, &ctx->job_meta, origin_lat,
                                               origin_lon);

    if (rc != GM_OK) {
        ctx->status = EXPORT_SCREEN_STATUS_CSV_ERROR;
        return;
    }

    ctx->status = used_usb ? EXPORT_SCREEN_STATUS_CSV_OK : EXPORT_SCREEN_STATUS_CSV_OK_FALLBACK;
    log_info("export_screen: wrote CSV for job '%s' (%u points) to %s (%s)", ctx->job_ctx->name,
             ctx->points.count, csv_path, used_usb ? "USB" : "internal storage");
}

/** See ui/core/widget.h's ui_grid_add_back_button() doc comment. */
static void on_back(UiWidget *self, void *screen_ctx)
{
    ExportScreenCtx *ctx = (ExportScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void export_screen_init(ExportScreenCtx *ctx, UiScreenStack *stack, const JobContext *job_ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack   = stack;
    ctx->job_ctx = job_ctx;

    job_metadata_defaults(&ctx->job_meta);
    measure_points_init(&ctx->points);

    ui_grid_init(&ctx->grid, ctx);

    UiRect r1 = { EXPORT_MARGIN, EXPORT_BTN_TOP_Y, EXPORT_BTN_W, EXPORT_BTN_H };
    UiRect r2 = { EXPORT_MARGIN, EXPORT_BTN_TOP_Y + (EXPORT_BTN_H + EXPORT_GAP), EXPORT_BTN_W,
                 EXPORT_BTN_H };

    ui_grid_add_button(&ctx->grid, r1, "Export LandXML", on_export_landxml);
    ui_grid_add_button(&ctx->grid, r2, "Export CSV", on_export_csv);
    ui_grid_add_back_button(&ctx->grid, on_back);
}

static void export_on_enter(void *raw_ctx)
{
    ExportScreenCtx *ctx = (ExportScreenCtx *)raw_ctx;
    reload_export_data(ctx);
    ui_grid_focus_first(&ctx->grid);
}

static bool export_on_event(void *raw_ctx, UiEvent ev)
{
    ExportScreenCtx *ctx = (ExportScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Measure Points */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen export_screen_as_ui_screen(ExportScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = export_on_enter;
    s.on_event  = export_on_event;
    s.on_render = export_screen_render;
    s.ctx       = ctx;
    return s;
}