/**
 * @file ui/screens/continue_project_screen_draw.c
 * @brief Continue Existing Project rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/continue_project_screen.h"
#include "ui/tft/display.h"

#include <string.h>

/* TITLE_Y/STATUS_Y relationship matches job_create_screen_draw.c and the
 * fixed version of open_job_screen_draw.c -- title at TITLE_Y (scale 2,
 * occupies TITLE_Y..TITLE_Y+14), status message at TITLE_Y+22, an 8px
 * gap below the title's actual rendered height. Defining STATUS_Y in
 * terms of TITLE_Y here from the start, rather than as an independent
 * constant, is exactly what open_job_screen_draw.c's overlap bug showed
 * is needed to keep the two from drifting out of sync. */
#define TITLE_Y  8
#define STATUS_Y (TITLE_Y + 22)

static const char *status_text(ContinueProjectStatus status)
{
    switch (status) {
    case CONTINUE_PROJECT_STATUS_NO_PROJECTS:
        return "No projects found -- start from New Project first";
    case CONTINUE_PROJECT_STATUS_NONE:
    default:
        return NULL;
    }
}

void continue_project_screen_render(void *raw_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Continue Existing Project";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, TITLE_Y, title, TFT_CYAN, TFT_BLACK, scale);

    const char *msg = status_text(ctx->status);
    if (msg) {
        uint16_t msg_w = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
        uint16_t mx    = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
        display_draw_string(mx, STATUS_Y, msg, TFT_ORANGE, TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}