/**
 * @file ui/screens/export_screen_draw.c
 * @brief Export screen rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/export_screen.h"
#include "ui/tft/display.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define EXPORT_TITLE_Y   8
#define EXPORT_SUMMARY_Y 60
#define EXPORT_STATUS_Y  100

/**
 * Returns the message text and whether it represents success (green)
 * or an error/info condition (orange) -- same two-color convention
 * job_create_screen_draw.c's own status_text() already establishes
 * for a status-message-below-title layout, extended here with a
 * success case that screen never needed (Job Create's own statuses
 * are all error conditions).
 */
static const char *status_text(ExportScreenStatus status, bool *out_is_success)
{
    *out_is_success = false;
    switch (status) {
    case EXPORT_SCREEN_STATUS_NO_JOB:
        return "No active job -- open or create one first";
    case EXPORT_SCREEN_STATUS_LOAD_ERROR:
        return "points.csv exists but could not be read";
    case EXPORT_SCREEN_STATUS_LANDXML_ERROR:
        return "LandXML export failed -- check storage";
    case EXPORT_SCREEN_STATUS_CSV_ERROR:
        return "CSV export failed -- check storage";
    case EXPORT_SCREEN_STATUS_LANDXML_OK:
        *out_is_success = true;
        return "LandXML exported to export/points.xml";
    case EXPORT_SCREEN_STATUS_CSV_OK:
        *out_is_success = true;
        return "CSV exported to export/points_export.csv";
    case EXPORT_SCREEN_STATUS_NONE:
    default:
        return NULL;
    }
}

void export_screen_render(void *raw_ctx)
{
    ExportScreenCtx *ctx = (ExportScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Export";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, EXPORT_TITLE_Y, title, TFT_CYAN, TFT_BLACK, scale);

    /* Job name + point count summary -- lets the crew confirm they're
     * about to export the job they think they are before pressing
     * either button, same reasoning Open Existing Job's job list shows
     * names rather than requiring blind selection. */
    char summary[96];
    const char *job_name =
        (ctx->job_ctx && ctx->job_ctx->name[0] != '\0') ? ctx->job_ctx->name : "(no job)";
    snprintf(summary, sizeof(summary), "Job: %s   Points: %u", job_name, ctx->points.count);
    uint16_t summary_w = (uint16_t)(strlen(summary) * (TFT_FONT_W + 1));
    uint16_t sx = (uint16_t)((TFT_WIDTH > summary_w) ? (TFT_WIDTH - summary_w) / 2 : 4);
    display_draw_string(sx, EXPORT_SUMMARY_Y, summary, TFT_WHITE, TFT_BLACK, 1);

    bool is_success = false;
    const char *msg = status_text(ctx->status, &is_success);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        display_draw_string(mx, EXPORT_STATUS_Y, msg, is_success ? TFT_GREEN : TFT_ORANGE,
                            TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}