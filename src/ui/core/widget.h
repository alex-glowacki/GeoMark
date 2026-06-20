/**
 * @file ui/core/widget.h
 * @brief Generic, hardware-agnostic widget model for the handheld UI redesign.
 *
 * Provides the focusable building blocks (label, button, text field,
 * numeric field, dropdown) that every screen in the new navigation tree
 * (sleep, main menu, new project, job setup, measure points, ...) is built
 * from. This replaces ad-hoc per-screen layout code — the pattern used in
 * the legacy ui/survey_screen.c — with one reusable model, so a new screen
 * only has to declare its widgets and wire behavior, not reinvent focus,
 * hit-testing, and rendering each time.
 *
 * Design notes:
 *   - Fixed-capacity, no heap allocation (UI_GRID_MAX_WIDGETS per screen),
 *     consistent with the rest of GeoMark (gm_point_store_t, SurveySession,
 *     etc. are all fixed-size). Raised from 24 to 50 to make room for a
 *     full on-screen QWERTY keyboard (41 keys, see ui/core/keyboard.h)
 *     sharing a grid with a screen's own form widgets.
 *   - This file + widget.c have zero dependency on ui/tft/display.h, so the
 *     logic is unit-testable on host with no SPI/GPIO/framebuffer present.
 *     Rendering lives in widget_draw.c, the only piece that touches
 *     display.h — swapping the SPI backend for the future fbdev backend
 *     (Item 11) will not require touching this file or widget.c at all.
 *   - Touch-tap unification (Item 11, decided): a UI_EVENT_TAP is hit-tested
 *     against widget rects, relocates focus to the tapped widget, then
 *     fires the exact same activation path UI_EVENT_ACTIVATE would. See
 *     ui_grid_handle_event() in widget.c.
 *
 * Ownership: label strings, dropdown options, numeric unit_suffix, and
 * text-field buf are all caller-owned and must outlive the widget — the
 * grid never copies or frees them (same convention as
 * SurveyScreenCtx::codelist in the legacy UI).
 */

#ifndef GEOMARK_UI_CORE_WIDGET_H
#define GEOMARK_UI_CORE_WIDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui/core/ui_event.h"

/* --------------------------------------------------------------------------
 * Geometry
 * -------------------------------------------------------------------------- */

typedef struct {
    uint16_t x, y, w, h;
} UiRect;

/* --------------------------------------------------------------------------
 * Widget kinds
 * -------------------------------------------------------------------------- */

typedef enum {
    WIDGET_LABEL = 0,     /* static text, never focusable                  */
    WIDGET_BUTTON,        /* label + on_activate callback                  */
    WIDGET_TEXT_FIELD,    /* editable string (keyboard wiring is Phase 2)  */
    WIDGET_NUMERIC_FIELD, /* value + step/min/max + optional unit suffix   */
    WIDGET_DROPDOWN,      /* fixed option list + selected index            */
} UiWidgetKind;

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

typedef struct UiWidget {
    UiWidgetKind kind;
    UiRect rect;
    const char *label; /* not owned; caption/title text, may be NULL    */
    bool focusable;
    bool focused; /* maintained by the grid — do not set directly */

    /**
     * Fired on UI_EVENT_ACTIVATE (Center / resolved tap), after any
     * kind-specific default behavior in ui_grid_handle_event() has already
     * been applied (numeric step, dropdown cycle, text-field edit toggle).
     * May be NULL. screen_ctx is UiWidgetGrid::screen_ctx, passed through
     * untouched so the callback can reach the owning screen's own state.
     */
    void (*on_activate)(struct UiWidget *self, void *screen_ctx);

    union {
        struct {
            char *buf; /* not owned; caller-allocated, NUL-terminated */
            size_t buf_cap;
            bool editing; /* true while the on-screen keyboard owns this
                           * field — keyboard wiring lands in Phase 2    */
        } text;

        struct {
            double value;
            double step;
            double min;
            double max;
            const char *unit_suffix; /* e.g. "ft" — not owned, may be NULL */
            uint8_t decimals;
        } numeric;

        struct {
            const char *const *options; /* not owned */
            uint32_t option_count;
            uint32_t selected;
        } dropdown;
    } as;
} UiWidget;

/* --------------------------------------------------------------------------
 * Widget grid — one screen's worth of focusable widgets
 * -------------------------------------------------------------------------- */

#define UI_GRID_MAX_WIDGETS 50

typedef struct {
    UiWidget widgets[UI_GRID_MAX_WIDGETS];
    uint32_t count;
    int32_t focus_idx; /* -1 == nothing focused                          */
    void *screen_ctx;  /* opaque; passed to every on_activate callback   */
} UiWidgetGrid;

/* --------------------------------------------------------------------------
 * Lifecycle / construction
 * -------------------------------------------------------------------------- */

void ui_grid_init(UiWidgetGrid *grid, void *screen_ctx);

UiWidget *ui_grid_add_label(UiWidgetGrid *grid, UiRect rect, const char *text);

UiWidget *ui_grid_add_button(UiWidgetGrid *grid, UiRect rect, const char *label,
                             void (*on_activate)(UiWidget *self, void *screen_ctx));

UiWidget *ui_grid_add_text_field(UiWidgetGrid *grid, UiRect rect, const char *label, char *buf,
                                 size_t buf_cap);

UiWidget *ui_grid_add_numeric_field(UiWidgetGrid *grid, UiRect rect, const char *label,
                                    double initial, double step, double min, double max,
                                    const char *unit_suffix, uint8_t decimals);

UiWidget *ui_grid_add_dropdown(UiWidgetGrid *grid, UiRect rect, const char *label,
                               const char *const *options, uint32_t option_count,
                               uint32_t initial_selected);

/* --------------------------------------------------------------------------
 * Focus / hit-testing / input
 * -------------------------------------------------------------------------- */

bool ui_widget_rect_contains(const UiRect *r, uint16_t x, uint16_t y);

/** Focus the first focusable widget in the grid. Returns false if none. */
bool ui_grid_focus_first(UiWidgetGrid *grid);

/**
 * Move focus geometrically in one direction (UI_EVENT_NAV_UP/DOWN/LEFT/RIGHT).
 * Returns false if there is no focusable widget that way (an edge) — the
 * caller (the owning screen) decides what an edge means, e.g. translating
 * a NAV_LEFT-at-edge into a UI_EVENT_BACK dispatched to the screen stack.
 */
bool ui_grid_move_focus(UiWidgetGrid *grid, UiEventType dir);

/** Index of the focusable widget at (x, y), or -1 if none. */
int32_t ui_grid_hit_test(const UiWidgetGrid *grid, uint16_t x, uint16_t y);

/**
 * Feed one UiEvent into the grid. Handles NAV_* (focus movement) and
 * ACTIVATE/TAP (kind-specific default behavior + on_activate callback).
 * Returns true if the grid did something with the event.
 */
bool ui_grid_handle_event(UiWidgetGrid *grid, UiEvent ev);

/* --------------------------------------------------------------------------
 * Rendering — implemented in widget_draw.c (depends on ui/tft/display.h)
 * -------------------------------------------------------------------------- */

void ui_grid_render(const UiWidgetGrid *grid);

#endif /* GEOMARK_UI_CORE_WIDGET_H */