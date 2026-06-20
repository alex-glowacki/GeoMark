/**
 * @file ui/screens/job_setup_screen_draw.c
 * @brief Job Setup rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/job_setup_screen.h"
#include "ui/tft/display.h"

#include <string.h>

void job_setup_screen_render(void *raw_ctx)
{
    JobSetupScreenCtx *ctx = (JobSetupScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Job Setup";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, 24, title, TFT_CYAN, TFT_BLACK, scale);

    ui_grid_render(&ctx->grid);
}