/**
 * @file ui/screens/job_create_screen_draw.c
 * @brief Create New Job screen rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/job_create_screen.h"
#include "ui/tft/display.h"

#include <string.h>

#define STATUS_Y 8

static const char *status_text(JobCreateStatus status)
{
    switch (status) {
    case JOB_CREATE_STATUS_EMPTY_NAME:
        return "Enter a job name first";
    case JOB_CREATE_STATUS_IO_ERROR:
        return "Could not create the job folder -- check storage";
    case JOB_CREATE_STATUS_NONE:
    default:
        return NULL;
    }
}

void job_create_screen_render(void *raw_ctx)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)raw_ctx;

    if (ctx->status != JOB_CREATE_STATUS_NONE &&
        ctx->job_name_len != ctx->status_job_name_len_snapshot) {
        ctx->status = JOB_CREATE_STATUS_NONE;
    }

    display_fill(TFT_BLACK);

    const char *title  = "Create New Job";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, STATUS_Y, title, TFT_CYAN, TFT_BLACK, scale);

    const char *msg = status_text(ctx->status);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        display_draw_string(mx, (uint16_t)(STATUS_Y + 22), msg, TFT_ORANGE, TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}