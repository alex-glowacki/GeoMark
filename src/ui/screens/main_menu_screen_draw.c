/**
 * @file ui/screens/main_menu_screen_draw.c
 * @brief Main menu rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/main_menu_screen.h"
#include "ui/tft/display.h"

#include <string.h>

void main_menu_screen_render(void *raw_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "GeoMark";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, 24, title, TFT_CYAN, TFT_BLACK, scale);

    ui_grid_render(&ctx->grid);
}