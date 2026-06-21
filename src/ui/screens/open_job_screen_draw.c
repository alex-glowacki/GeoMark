/**
 * @file ui/screens/open_job_screen_draw.c
 * @brief Open Existing Job rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/open_job_screen.h"
#include "ui/tft/display.h"

#include <string.h>

#define STATUS_Y 30

static const char *status_text(OpenJobStatus status)
{
    switch (status) {
    case OPEN_JOB_STATUS_NO_PROJECT:
        return "No active project -- start from New Project first";
    case OPEN_JOB_STATUS_NO_JOBS:
        return "No jobs found in this project yet";
    case OPEN_JOB_STATUS_LOAD_ERROR:
        return "Could not load that job -- check storage";
    case OPEN_JOB_STATUS_NONE:
    default:
        return NULL;
    }
}

void open_job_screen_render(void *raw_ctx)
{
    OpenJobScreenCtx *ctx = (OpenJobScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Open Existing Job";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, 24, title, TFT_CYAN, TFT_BLACK, scale);

    const char *msg = status_text(ctx->status);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        display_draw_string(mx, STATUS_Y, msg, TFT_ORANGE, TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}