/**
 * @file ui/screens/sleep_screen_draw.c
 * @brief Sleep screen rendering.
 *
 * Placeholder content — exact custom image/text is still open in
 * geomark-ui-redesign-decisions.md §5. Swap this out once that's decided;
 * nothing else in the screen needs to change since on_render is the only
 * thing touching display.h.
 */

#define _GNU_SOURCE

#include "ui/screens/sleep_screen.h"
#include "ui/tft/display.h"

#include <string.h>

void sleep_screen_render(void *raw_ctx) {
    (void)raw_ctx;

    display_fill(TFT_BLACK);

    const char *title = "GeoMark";
    const char *subtitle = "Tap to begin";

    uint8_t scale = 3;
    uint16_t title_w = (uint16_t)(strlen(title) * (TFT_FONT_W + 1) * scale);
    uint16_t tx = (uint16_t)((TFT_WIDTH > title_w) ? (TFT_WIDTH - title_w) / 2 : 4);
    uint16_t ty = (uint16_t)(TFT_HEIGHT / 2 - 30);
    display_draw_string(tx, ty, title, TFT_WHITE, TFT_BLACK, scale);

    uint16_t sub_w = (uint16_t)(strlen(subtitle) * (TFT_FONT_W + 1));
    uint16_t sx = (uint16_t)((TFT_WIDTH > sub_w) ? (TFT_WIDTH - sub_w) / 2 : 4);
    uint16_t sy = (uint16_t)(ty + TFT_FONT_H * scale + 16);
    display_draw_string(sx, sy, subtitle, TFT_GRAY, TFT_BLACK, 1);
}