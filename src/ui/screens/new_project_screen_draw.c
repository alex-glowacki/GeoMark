/**
 * @file ui/screens/new_project_screen_draw.c
 * @brief New Project screen rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/new_project_screen.h"
#include "ui/tft/display.h"

#include <string.h>

#define STATUS_Y 130

static const char *status_text(NewProjectStatus status)
{
    switch (status) {
    case NEW_PROJECT_STATUS_EMPTY_NAME:
        return "Enter a project name first";
    case NEW_PROJECT_STATUS_ALREADY_EXISTS:
        return "A project with that name already exists";
    case NEW_PROJECT_STATUS_IO_ERROR:
        return "Could not create the project folder -- check storage";
    case NEW_PROJECT_STATUS_NONE:
    default:
        return NULL;
    }
}

void new_project_screen_render(void *raw_ctx)
{
    NewProjectScreenCtx *ctx = (NewProjectScreenCtx *)raw_ctx;

    if (ctx->status != NEW_PROJECT_STATUS_NONE &&
        ctx->name_len != ctx->name_len_at_status) {
        ctx->status = NEW_PROJECT_STATUS_NONE;
    }

    display_fill(TFT_BLACK);

    const char *title  = "New Project";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, 16, title, TFT_CYAN, TFT_BLACK, scale);

    const char *msg = status_text(ctx->status);
    if (msg) {
        display_draw_string(20, STATUS_Y, msg, TFT_ORANGE, TFT_BLACK, 1);
    }

    ui_grid_render(&ctx->grid);
}