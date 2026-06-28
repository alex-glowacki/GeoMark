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
 * Returns the message text and which of three visual treatments it
 * gets: success (green, written to the USB drive), fallback (yellow,
 * written to internal storage because the USB drive was not mounted --
 * still a successful export, but the crew needs to know it isn't on
 * the drive they expect to walk away with), or error/info (orange).
 * Same status-message-below-title layout job_create_screen_draw.c's
 * own status_text() already establishes, extended here with the two
 * cases that screen never needed (Job Create's own statuses are all
 * plain error conditions, with no successful-but-worth-flagging case).
 */
typedef enum {
    EXPORT_MSG_INFO_OR_ERROR = 0,
    EXPORT_MSG_SUCCESS,
    EXPORT_MSG_FALLBACK,
} ExportMsgKind;

static const char *status_text(ExportScreenStatus status, ExportMsgKind *out_kind)
{
    *out_kind = EXPORT_MSG_INFO_OR_ERROR;
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
        *out_kind = EXPORT_MSG_SUCCESS;
        return "LandXML exported to USB drive";
    case EXPORT_SCREEN_STATUS_CSV_OK:
        *out_kind = EXPORT_MSG_SUCCESS;
        return "CSV exported to USB drive";
    case EXPORT_SCREEN_STATUS_LANDXML_OK_FALLBACK:
        *out_kind = EXPORT_MSG_FALLBACK;
        return "USB drive not found -- LandXML saved internally instead";
    case EXPORT_SCREEN_STATUS_CSV_OK_FALLBACK:
        *out_kind = EXPORT_MSG_FALLBACK;
        return "USB drive not found -- CSV saved internally instead";
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

    ExportMsgKind kind = EXPORT_MSG_INFO_OR_ERROR;
    const char *msg = status_text(ctx->status, &kind);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        uint16_t color = (kind == EXPORT_MSG_SUCCESS)  ? TFT_GREEN
                        : (kind == EXPORT_MSG_FALLBACK) ? TFT_YELLOW
                                                        : TFT_ORANGE;
        display_draw_string(mx, EXPORT_STATUS_Y, msg, color, TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}