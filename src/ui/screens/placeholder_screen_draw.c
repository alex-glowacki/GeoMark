/**
 * @file ui/screens/placeholder_screen_draw.c
 * @brief Placeholder screen rendering.
 */

#define _GNU_SOURCE

#include "ui/screens/placeholder_screen.h"
#include "ui/tft/display.h"

#include <string.h>

void placeholder_screen_render(void *raw_ctx)
{
    PlaceholderScreenCtx *ctx = (PlaceholderScreenCtx *)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title  = "Coming Soon";
    uint8_t  scale      = 2;
    uint16_t title_w    = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx         = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    display_draw_string(tx, (uint16_t)(TFT_HEIGHT / 2 - 30), title, TFT_YELLOW, TFT_BLACK, scale);

    const char *msg = ctx->message ? ctx->message : "";
    uint16_t msg_w   = (uint16_t)(strlen(msg) * (TFT_FONT_W + 1));
    uint16_t mx      = (uint16_t)((TFT_WIDTH > msg_w) ? (TFT_WIDTH - msg_w) / 2 : 4);
    display_draw_string(mx, (uint16_t)(TFT_HEIGHT / 2 + 10), msg, TFT_GRAY, TFT_BLACK, 1);

    const char *hint = "Press Left to go back";
    uint16_t hint_w   = (uint16_t)(strlen(hint) * (TFT_FONT_W + 1));
    uint16_t hx       = (uint16_t)((TFT_WIDTH > hint_w) ? (TFT_WIDTH - hint_w) / 2 : 4);
    display_draw_string(hx, (uint16_t)(TFT_HEIGHT - 30), hint, TFT_DKGRAY, TFT_BLACK, 1);
}