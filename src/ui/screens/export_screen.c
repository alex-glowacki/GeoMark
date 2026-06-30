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

#define EXPORT_MARGIN   20
#define EXPORT_BTN_H    56
#define EXPORT_GAP      18
#define EXPORT_BTN_W    (TFT_WIDTH - 2 * EXPORT_MARGIN)
#define EXPORT_BTN_TOP_Y 150

/* -------------------------------------------------------------------------
 * Reload -- same "read the real on-disk state every time this screen
 * becomes visible" stance measure_points_screen.c and open_job_screen.c
 * already each establish.
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
    job_metadata_load(ini_path, &ctx->job_meta);

    char csv_path[600];
    measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
    gm_status_t rc = measure_points_load_csv(csv_path, &ctx->points);
    if (rc != GM_OK) {
        ctx->status = EXPORT_SCREEN_STATUS_LOAD_ERROR;
        return;
    }

    ctx->status = EXPORT_SCREEN_STATUS_NONE;
}

/* "First point in the store sets the origin" -- same convention
 * measure_points_screen.c's reload_job_data() already establishes. */
static void resolve_origin(const MeasurePointStore *points,
                           double *origin_lat, double *origin_lon)
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
 * Destination resolution
 *
 * Tries the USB drive first (if mounted); falls back to the internal
 * job_dir/export/ location. usb_export_path_for_job() resolves both
 * the LandXML and PNEZD CSV destination paths together in one call.
 * The PNEZD path is derived by replacing "points_export.csv" with
 * "points_pnezd.csv" in the resolved csv_path, keeping the same
 * directory on whichever destination won (USB or internal).
 * ---------------------------------------------------------------------- */

static bool resolve_export_destination(const JobContext *job_ctx,
                                       char *out_xml_path, size_t xml_len,
                                       char *out_pnezd_path, size_t pnezd_len)
{
    /* usb_export_path_for_job() fills both xml and csv paths. We use
     * the csv path only to derive the pnezd path (same directory,
     * different filename). A temporary csv_path buffer is sufficient. */
    char csv_path[USB_EXPORT_PATH_MAX];

    if (usb_export_is_mounted() &&
        usb_export_path_for_job(job_ctx->job_dir,
                                out_xml_path, xml_len,
                                csv_path, sizeof(csv_path)) == GM_OK) {
        /* Replace filename: "points_export.csv" -> "points_pnezd.csv"
         * in the same USB directory. Find the last '/' and rebuild. */
        char *last_slash = strrchr(csv_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            snprintf(out_pnezd_path, pnezd_len, "%s/points_pnezd.csv", csv_path);
        } else {
            snprintf(out_pnezd_path, pnezd_len, "%s", csv_path);
        }
        return true;
    }

    /* Internal storage fallback. */
    measure_points_export_landxml_path(job_ctx->job_dir, out_xml_path, xml_len);
    measure_points_export_pnezd_path(job_ctx->job_dir, out_pnezd_path, pnezd_len);
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
    char pnezd_path[USB_EXPORT_PATH_MAX];
    bool used_usb = resolve_export_destination(ctx->job_ctx,
                                               xml_path, sizeof(xml_path),
                                               pnezd_path, sizeof(pnezd_path));

    gm_status_t rc = measure_points_export_landxml(xml_path, &ctx->points,
                                                   &ctx->job_meta,
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

    char xml_path[USB_EXPORT_PATH_MAX]; /* unused by this callback */
    char pnezd_path[USB_EXPORT_PATH_MAX];
    bool used_usb = resolve_export_destination(ctx->job_ctx,
                                               xml_path, sizeof(xml_path),
                                               pnezd_path, sizeof(pnezd_path));

    gm_status_t rc = measure_points_export_pnezd(pnezd_path, &ctx->points,
                                                  &ctx->job_meta,
                                                  origin_lat, origin_lon);
    if (rc != GM_OK) {
        ctx->status = EXPORT_SCREEN_STATUS_CSV_ERROR;
        return;
    }

    ctx->status = used_usb ? EXPORT_SCREEN_STATUS_CSV_OK
                           : EXPORT_SCREEN_STATUS_CSV_OK_FALLBACK;
    log_info("export_screen: wrote PNEZD CSV for job '%s' (%u points) to %s (%s)",
             ctx->job_ctx->name, ctx->points.count, pnezd_path,
             used_usb ? "USB" : "internal storage");
}

static void on_back(UiWidget *self, void *screen_ctx)
{
    ExportScreenCtx *ctx = (ExportScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void export_screen_init(ExportScreenCtx *ctx, UiScreenStack *stack,
                        const JobContext *job_ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack   = stack;
    ctx->job_ctx = job_ctx;

    job_metadata_defaults(&ctx->job_meta);
    measure_points_init(&ctx->points);

    ui_grid_init(&ctx->grid, ctx);

    UiRect r1 = { EXPORT_MARGIN, EXPORT_BTN_TOP_Y,
                  EXPORT_BTN_W, EXPORT_BTN_H };
    UiRect r2 = { EXPORT_MARGIN, EXPORT_BTN_TOP_Y + EXPORT_BTN_H + EXPORT_GAP,
                  EXPORT_BTN_W, EXPORT_BTN_H };

    ui_grid_add_button(&ctx->grid, r1, "Export LandXML", on_export_landxml);
    ui_grid_add_button(&ctx->grid, r2, "Export CSV (PNEZD)", on_export_csv);
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
        return false;

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