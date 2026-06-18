/**
 * @file ui/core/widget_draw.c
 * @brief Widget grid rendering. The only file in the widget module that
 *        depends on ui/tft/display.h — kept separate from widget.c so the
 *        focus/hit-test/event logic stays unit-testable on host with no
 *        display hardware present, and so the eventual SPI → fbdev backend
 *        swap (Item 11) touches this file only.
 */

#define _GNU_SOURCE

#include "ui/core/widget.h"

#include <stdio.h>
#include <string.h>

#include "ui/tft/display.h"

#define UI_COL_BG       TFT_BLACK
#define UI_COL_BTN_BG   TFT_DKGRAY
#define UI_COL_BTN_FG   TFT_WHITE
#define UI_COL_LABEL_FG TFT_GRAY
#define UI_COL_FIELD_BG TFT_DKGRAY
#define UI_COL_FIELD_FG TFT_WHITE
#define UI_COL_FOCUS    TFT_YELLOW

static void draw_focus_border(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    display_fill_rect(x,         y,         w, 2, UI_COL_FOCUS);
    display_fill_rect(x,         y + h - 2, w, 2, UI_COL_FOCUS);
    display_fill_rect(x,         y,         2, h, UI_COL_FOCUS);
    display_fill_rect(x + w - 2, y,         2, h, UI_COL_FOCUS);
}

static void draw_centered_text(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t text_w = (uint16_t)(strlen(s) * (TFT_FONT_W + 1) * scale);
    uint16_t text_h = (uint16_t)(TFT_FONT_H * scale);
    uint16_t tx = (uint16_t)(x + (w > text_w ? (w - text_w) / 2 : 2));
    uint16_t ty = (uint16_t)(y + (h > text_h ? (h - text_h) / 2 : 2));
    display_draw_string(tx, ty, s, fg, bg, scale);
}

static void render_widget(const UiWidget *w)
{
    const UiRect *r = &w->rect;
    char buf[64];

    switch (w->kind) {
    case WIDGET_LABEL:
        display_draw_string(r->x, r->y, w->label ? w->label : "",
                            UI_COL_LABEL_FG, UI_COL_BG, 1);
        break;

    case WIDGET_BUTTON:
        display_fill_rect(r->x, r->y, r->w, r->h, UI_COL_BTN_BG);
        draw_centered_text(r->x, r->y, r->w, r->h, w->label ? w->label : "",
                           UI_COL_BTN_FG, UI_COL_BTN_BG, 1);
        break;

    case WIDGET_TEXT_FIELD:
        display_fill_rect(r->x, r->y, r->w, r->h, UI_COL_FIELD_BG);
        display_draw_string((uint16_t)(r->x + 4), (uint16_t)(r->y + 4),
                            w->as.text.buf ? w->as.text.buf : "",
                            UI_COL_FIELD_FG, UI_COL_FIELD_BG, 1);
        break;

    case WIDGET_NUMERIC_FIELD:
        display_fill_rect(r->x, r->y, r->w, r->h, UI_COL_FIELD_BG);
        if (w->as.numeric.unit_suffix)
            snprintf(buf, sizeof(buf), "%.*f %s", w->as.numeric.decimals,
                     w->as.numeric.value, w->as.numeric.unit_suffix);
        else
            snprintf(buf, sizeof(buf), "%.*f", w->as.numeric.decimals, w->as.numeric.value);
        display_draw_string((uint16_t)(r->x + 4), (uint16_t)(r->y + 4),
                            buf, UI_COL_FIELD_FG, UI_COL_FIELD_BG, 1);
        break;

    case WIDGET_DROPDOWN:
        display_fill_rect(r->x, r->y, r->w, r->h, UI_COL_FIELD_BG);
        if (w->as.dropdown.option_count > 0 &&
            w->as.dropdown.selected < w->as.dropdown.option_count) {
            display_draw_string((uint16_t)(r->x + 4), (uint16_t)(r->y + 4),
                                w->as.dropdown.options[w->as.dropdown.selected],
                                UI_COL_FIELD_FG, UI_COL_FIELD_BG, 1);
        }
        break;
    }

    if (w->focused && w->focusable)
        draw_focus_border(r->x, r->y, r->w, r->h);
}

void ui_grid_render(const UiWidgetGrid *grid)
{
    for (uint32_t i = 0; i < grid->count; i++)
        render_widget(&grid->widgets[i]);
}