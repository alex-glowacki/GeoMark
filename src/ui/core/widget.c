/**
 * @file ui/core/widget.c
 * @brief Widget grid logic — construction, focus navigation, hit-testing,
 *        event handling. No ui/tft/display.h dependency by design; see
 *        widget_draw.c for rendering. This split is what makes
 *        tests/test_widget.c runnable on host with no display hardware.
 */

#define _GNU_SOURCE

#include "ui/core/widget.h"

#include <string.h>

#include "util/log.h"

void ui_grid_init(UiWidgetGrid *grid, void *screen_ctx)
{
    memset(grid, 0, sizeof(*grid));
    grid->focus_idx  = -1;
    grid->screen_ctx = screen_ctx;
}

static UiWidget *grid_alloc(UiWidgetGrid *grid, const char *what)
{
    if (grid->count >= UI_GRID_MAX_WIDGETS) {
        log_error("ui/widget: grid full (max %d), dropped %s",
                  UI_GRID_MAX_WIDGETS, what ? what : "widget");
        return NULL;
    }
    UiWidget *w = &grid->widgets[grid->count++];
    memset(w, 0, sizeof(*w));
    return w;
}

UiWidget *ui_grid_add_label(UiWidgetGrid *grid, UiRect rect, const char *text)
{
    UiWidget *w = grid_alloc(grid, text);
    if (!w) return NULL;
    w->kind      = WIDGET_LABEL;
    w->rect      = rect;
    w->label     = text;
    w->focusable = false;
    return w;
}

UiWidget *ui_grid_add_button(UiWidgetGrid *grid, UiRect rect, const char *label,
                             void (*on_activate)(UiWidget *self, void *screen_ctx))
{
    UiWidget *w = grid_alloc(grid, label);
    if (!w) return NULL;
    w->kind        = WIDGET_BUTTON;
    w->rect        = rect;
    w->label       = label;
    w->focusable   = true;
    w->on_activate = on_activate;
    return w;
}

UiWidget *ui_grid_add_text_field(UiWidgetGrid *grid, UiRect rect, const char *label,
                                 char *buf, size_t buf_cap)
{
    UiWidget *w = grid_alloc(grid, label);
    if (!w) return NULL;
    w->kind            = WIDGET_TEXT_FIELD;
    w->rect            = rect;
    w->label           = label;
    w->focusable       = true;
    w->as.text.buf     = buf;
    w->as.text.buf_cap = buf_cap;
    w->as.text.editing = false;
    return w;
}

UiWidget *ui_grid_add_numeric_field(UiWidgetGrid *grid, UiRect rect, const char *label,
                                    double initial, double step, double min, double max,
                                    const char *unit_suffix, uint8_t decimals)
{
    UiWidget *w = grid_alloc(grid, label);
    if (!w) return NULL;
    w->kind                   = WIDGET_NUMERIC_FIELD;
    w->rect                   = rect;
    w->label                  = label;
    w->focusable              = true;
    w->as.numeric.value       = initial;
    w->as.numeric.step        = step;
    w->as.numeric.min         = min;
    w->as.numeric.max         = max;
    w->as.numeric.unit_suffix = unit_suffix;
    w->as.numeric.decimals    = decimals;
    return w;
}

UiWidget *ui_grid_add_dropdown(UiWidgetGrid *grid, UiRect rect, const char *label,
                               const char *const *options, uint32_t option_count,
                               uint32_t initial_selected)
{
    UiWidget *w = grid_alloc(grid, label);
    if (!w) return NULL;
    w->kind                     = WIDGET_DROPDOWN;
    w->rect                     = rect;
    w->label                    = label;
    w->focusable                = true;
    w->as.dropdown.options      = options;
    w->as.dropdown.option_count = option_count;
    w->as.dropdown.selected     = (option_count > 0) ? (initial_selected % option_count) : 0;
    return w;
}

void ui_widget_mark_scrollable(UiWidget *w)
{
    if (!w) return;
    w->scrollable = true;
}

static bool scroll_region_active(const UiWidgetGrid *grid)
{
    return grid->scroll_region.w > 0 && grid->scroll_region.h > 0;
}

void ui_grid_set_scroll_region(UiWidgetGrid *grid, UiRect region)
{
    grid->scroll_region = region;
    grid->scroll_y       = 0;
}

bool ui_widget_rect_contains(const UiRect *r, uint16_t x, uint16_t y)
{
    return x >= r->x && x < (uint16_t)(r->x + r->w) &&
           y >= r->y && y < (uint16_t)(r->y + r->h);
}

static int32_t rect_center_x(const UiRect *r) { return (int32_t)r->x + (int32_t)r->w / 2; }
static int32_t rect_center_y(const UiRect *r) { return (int32_t)r->y + (int32_t)r->h / 2; }

/**
 * The rect a widget actually renders/hit-tests at when its effective
 * (scrolled) position is on-screen, i.e. effective y >= 0.
 *
 * UiRect.y is uint16_t, so this function cannot represent a widget
 * scrolled above y=0 (a negative effective position) without either
 * wrapping (corrupting the value) or saturating (which would make a
 * far-off-screen widget look identical to one sitting exactly at the
 * region's top edge to any caller that only looks at this return value).
 * Every caller that needs a correct in/out-of-view decision -- hit
 * testing, render-skip, and the auto-scroll-into-view logic in
 * ui_grid_move_focus() -- therefore computes the signed effective
 * top/bottom itself from rect.y and scroll_y directly, and only calls
 * this function once it already knows the result will be non-negative
 * (e.g. ui_grid_render() checks overlap in signed space first, then
 * calls this only for widgets it has already decided to draw).
 */
UiRect ui_widget_effective_rect(const UiWidget *w, const UiWidgetGrid *grid)
{
    UiRect r = w->rect;
    if (w->scrollable) {
        int32_t shifted = (int32_t)r.y - grid->scroll_y;
        r.y = (shifted < 0) ? 0 : (uint16_t)shifted;
    }
    return r;
}

bool ui_grid_focus_first(UiWidgetGrid *grid)
{
    for (uint32_t i = 0; i < grid->count; i++) {
        if (grid->widgets[i].focusable) {
            if (grid->focus_idx >= 0 && grid->focus_idx < (int32_t)grid->count)
                grid->widgets[grid->focus_idx].focused = false;
            grid->focus_idx = (int32_t)i;
            grid->widgets[i].focused = true;
            grid->scroll_y = 0;
            return true;
        }
    }
    grid->focus_idx = -1;
    return false;
}

bool ui_grid_move_focus(UiWidgetGrid *grid, UiEventType dir)
{
    if (grid->focus_idx < 0 || grid->focus_idx >= (int32_t)grid->count)
        return ui_grid_focus_first(grid);

    const UiWidget *cur = &grid->widgets[grid->focus_idx];
    int32_t cx = rect_center_x(&cur->rect);
    int32_t cy = rect_center_y(&cur->rect);

    int32_t best_idx   = -1;
    int64_t best_score = 0;

    for (uint32_t i = 0; i < grid->count; i++) {
        if ((int32_t)i == grid->focus_idx) continue;
        const UiWidget *cand = &grid->widgets[i];
        if (!cand->focusable) continue;

        int32_t dx = rect_center_x(&cand->rect) - cx;
        int32_t dy = rect_center_y(&cand->rect) - cy;
        int32_t primary, perp;

        switch (dir) {
        case UI_EVENT_NAV_RIGHT: if (dx <= 0) continue; primary = dx;  perp = dy; break;
        case UI_EVENT_NAV_LEFT:  if (dx >= 0) continue; primary = -dx; perp = dy; break;
        case UI_EVENT_NAV_DOWN:  if (dy <= 0) continue; primary = dy;  perp = dx; break;
        case UI_EVENT_NAV_UP:    if (dy >= 0) continue; primary = -dy; perp = dx; break;
        default: continue;
        }

        /* Weight the perpendicular offset heavily so navigation prefers
         * widgets aligned with the current one over diagonal jumps. */
        int64_t score = (int64_t)primary * primary + (int64_t)perp * perp * 4;

        if (best_idx < 0 || score < best_score) {
            best_idx   = (int32_t)i;
            best_score = score;
        }
    }

    if (best_idx < 0)
        return false;

    grid->widgets[grid->focus_idx].focused = false;
    grid->focus_idx = best_idx;
    grid->widgets[best_idx].focused = true;

    /* If scrolling is enabled and the newly-focused widget is a
     * scrollable one currently outside the visible region, scroll just
     * enough to bring it fully into view -- never leave focus on
     * something the person can't see. Non-scrollable widgets (fixed
     * chrome) are never affected.
     *
     * Computed directly in signed arithmetic rather than by reading
     * ui_widget_effective_rect()'s UiRect back out: UiRect.y is
     * uint16_t, which cannot represent a widget that has scrolled above
     * the viewport (a legitimately negative effective Y) -- casting a
     * negative value into it wraps around to a huge positive number
     * instead (e.g. -16 -> 65520), corrupting every comparison and the
     * scroll_y accumulator itself. The fix is to never let a negative
     * effective Y round-trip through a uint16_t at all: do the
     * comparison here in plain int32_t, using the widget's literal
     * (always non-negative, since UiRect.y is unsigned) rect.y directly. */
    if (scroll_region_active(grid)) {
        const UiWidget *focused = &grid->widgets[best_idx];
        if (focused->scrollable) {
            int32_t eff_top    = (int32_t)focused->rect.y - grid->scroll_y;
            int32_t eff_bottom = eff_top + (int32_t)focused->rect.h;
            const UiRect *region = &grid->scroll_region;

            if (eff_top < (int32_t)region->y) {
                grid->scroll_y -= (int32_t)region->y - eff_top;
            } else if (eff_bottom > (int32_t)(region->y + region->h)) {
                grid->scroll_y += eff_bottom - (int32_t)(region->y + region->h);
            }
            if (grid->scroll_y < 0)
                grid->scroll_y = 0;
        }
    }

    return true;
}

int32_t ui_grid_hit_test(const UiWidgetGrid *grid, uint16_t x, uint16_t y)
{
    for (uint32_t i = 0; i < grid->count; i++) {
        const UiWidget *w = &grid->widgets[i];
        if (!w->focusable) continue;

        int32_t eff_top = w->scrollable ? (int32_t)w->rect.y - grid->scroll_y
                                        : (int32_t)w->rect.y;
        if (eff_top < 0) continue; /* scrolled above the screen -- can't be tapped */

        UiRect eff = w->rect;
        eff.y = (uint16_t)eff_top;
        if (ui_widget_rect_contains(&eff, x, y))
            return (int32_t)i;
    }
    return -1;
}

static void activate_widget(UiWidgetGrid *grid, int32_t idx)
{
    UiWidget *w = &grid->widgets[idx];

    switch (w->kind) {
    case WIDGET_NUMERIC_FIELD: {
        double v = w->as.numeric.value + w->as.numeric.step;
        if (v > w->as.numeric.max) v = w->as.numeric.max;
        if (v < w->as.numeric.min) v = w->as.numeric.min;
        w->as.numeric.value = v;
        break;
    }
    case WIDGET_DROPDOWN:
        if (w->as.dropdown.option_count > 0)
            w->as.dropdown.selected =
                (w->as.dropdown.selected + 1) % w->as.dropdown.option_count;
        break;
    case WIDGET_TEXT_FIELD:
        w->as.text.editing = !w->as.text.editing;
        break;
    case WIDGET_BUTTON:
    case WIDGET_LABEL:
    default:
        break;
    }

    if (w->on_activate)
        w->on_activate(w, grid->screen_ctx);
}

bool ui_grid_handle_event(UiWidgetGrid *grid, UiEvent ev)
{
    switch (ev.type) {
    case UI_EVENT_NAV_UP:
    case UI_EVENT_NAV_DOWN:
    case UI_EVENT_NAV_LEFT:
    case UI_EVENT_NAV_RIGHT:
        return ui_grid_move_focus(grid, ev.type);

    case UI_EVENT_ACTIVATE:
        if (grid->focus_idx < 0 || grid->focus_idx >= (int32_t)grid->count)
            return false;
        activate_widget(grid, grid->focus_idx);
        return true;

    case UI_EVENT_TAP: {
        int32_t idx = ui_grid_hit_test(grid, ev.x, ev.y);
        if (idx < 0)
            return false;
        if (idx != grid->focus_idx) {
            if (grid->focus_idx >= 0 && grid->focus_idx < (int32_t)grid->count)
                grid->widgets[grid->focus_idx].focused = false;
            grid->focus_idx = idx;
            grid->widgets[idx].focused = true;
        }
        activate_widget(grid, idx);
        return true;
    }

    case UI_EVENT_BACK:
    case UI_EVENT_NONE:
    default:
        return false;
    }
}