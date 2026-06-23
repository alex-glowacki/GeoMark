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
 *     sharing a grid with a screen's own form widgets, then from 50 to 80
 *     when Job Create's Properties form (20 widgets: label+field/dropdown
 *     pairs across 9 rows plus a Properties heading and a Create button)
 *     plus the keyboard (41) totaled 61, exceeding 50. sizeof(UiWidget) is
 *     88 bytes as of this writing, so 80 widgets costs ~6.9KB per grid --
 *     trivial on a Pi 5, and this is one grid per active screen, not
 *     per-frame.
 *   - This file + widget.c have zero dependency on ui/tft/display.h, so the
 *     logic is unit-testable on host with no SPI/GPIO/framebuffer present.
 *     Rendering lives in widget_draw.c, the only piece that touches
 *     display.h — swapping the SPI backend for the future fbdev backend
 *     (Item 11) will not require touching this file or widget.c at all.
 *   - Touch-tap unification (Item 11, decided): a UI_EVENT_TAP is hit-tested
 *     against widget rects, relocates focus to the tapped widget, then
 *     fires the exact same activation path UI_EVENT_ACTIVATE would. See
 *     ui_grid_handle_event() in widget.c.
 *   - D-pad-driven scroll (added for Job Setup's 8-field Properties
 *     section, which doesn't fit on screen at once above an
 *     always-visible keyboard): see UiWidgetGrid::scroll_region's doc
 *     comment. Touch drag-to-scroll is a deliberate, documented gap --
 *     not yet built, see that same comment for why.
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
     * True if this widget scrolls with the grid's scroll region (see
     * UiWidgetGrid::scroll_y below); false for fixed chrome that always
     * renders at its literal rect regardless of scroll position (e.g.
     * keyboard keys, a screen title). Set via ui_widget_mark_scrollable()
     * after adding the widget, not directly -- default false
     * (grid_alloc()'s zero-init leaves every new widget non-scrollable
     * until marked).
     */
    bool scrollable;

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

#define UI_GRID_MAX_WIDGETS 80

typedef struct {
    UiWidget widgets[UI_GRID_MAX_WIDGETS];
    uint32_t count;
    int32_t focus_idx; /* -1 == nothing focused                          */
    void *screen_ctx;  /* opaque; passed to every on_activate callback   */

    /**
     * D-pad-driven vertical scrolling for forms with more scrollable
     * widgets than fit in scroll_region's height at once (e.g. Job Setup's
     * 8 Properties fields above an always-visible keyboard). Touch
     * drag-to-scroll is a known, deliberate gap -- not yet built, since it
     * needs new gesture-tracking in ui/core/touch_input.c (currently
     * tap-only, discarding in-progress contact position by design) that
     * risks destabilizing Session 20's hardware-verified touch edge-
     * detection fix. D-pad is the only scroll trigger today; revisit touch
     * drag as its own, separately-tested piece of work.
     *
     * scroll_y is in pixels, 0 = scrolled to the top. Only widgets with
     * scrollable == true are affected: their rendered position is
     * rect.y - scroll_y, and they're skipped entirely by ui_grid_render()
     * if that shifted position falls fully outside scroll_region. Fixed
     * chrome (scrollable == false) always renders at its literal rect.
     *
     * scroll_region with all-zero fields (the ui_grid_init() default)
     * means "no scrolling" -- ui_grid_move_focus() and ui_grid_render()
     * both treat a zero-area scroll_region as "scrolling disabled" and
     * behave exactly as before this field was added, so existing screens
     * (Main Menu, Sleep, New Project) are unaffected without any change
     * on their part.
     */
    UiRect scroll_region;
    int32_t scroll_y;
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
 * Back button — touch-only replacement for the physical Left/BACK button.
 *
 * Fixed top-left rect, identical on every screen, so the person always
 * knows where to tap regardless of which screen is showing. Placed at
 * UI_BACK_BUTTON_Y=4..32, comfortably above every screen's own centered
 * title text (drawn at y=8 or y=24 depending on the screen, scale 2,
 * 14px tall -- confirmed against every *_screen_draw.c in this codebase)
 * and above Measure Points' PANEL_TOP_Y=40 map/status panel, so this rect
 * is safe to add unconditionally without checking each screen's own
 * layout. x=8..78 is also clear of every screen's title at every title
 * string length actually used (shortest realistic centered title still
 * leaves tx > 170 -- see "Continue Existing Project", the longest title
 * in this codebase, computed against TFT_WIDTH=800).
 *
 * Like ui_grid_add_button(), the caller still supplies on_activate --
 * this helper does not call ui_stack_pop() or ui_stack_dispatch_event()
 * itself, since it has no UiScreenStack* of its own (grid->screen_ctx is
 * opaque to this module). Every screen's own on_back() callback should
 * dispatch a UI_EVENT_BACK through ui_stack_dispatch_event() rather than
 * popping directly -- that reuses each screen's own existing UI_EVENT_BACK
 * handling in on_event() (e.g. Measure Points closing an open overlay
 * first) instead of duplicating that logic here. See each screen's own
 * on_back() for the one-line pattern.
 * -------------------------------------------------------------------------- */

#define UI_BACK_BUTTON_X 8
#define UI_BACK_BUTTON_Y 4
#define UI_BACK_BUTTON_W 70
#define UI_BACK_BUTTON_H 28

/**
 * Add the standard "< Back" button at the fixed top-left rect above.
 * Same caller-supplied-on_activate convention as ui_grid_add_button() --
 * see that function and the file-level doc comment above for why. Call
 * once per grid construction (including every rebuild_grid()-style call
 * on screens that tear down and re-add their widget set, e.g.
 * ui/screens/measure_points_screen.c), the same as any other
 * ui_grid_add_*() call.
 */
UiWidget *ui_grid_add_back_button(UiWidgetGrid *grid,
                                  void (*on_activate)(UiWidget *self, void *screen_ctx));

/* --------------------------------------------------------------------------
 * Nav buttons — touch-only replacement for the physical d-pad's Up/Down,
 * scoped to vertical scroll only (see ui_grid_move_focus()'s doc comment:
 * there is no scroll_x anywhere in this struct, so Left/Right have no
 * scrolling role here -- Up/Down covers every scroll_region use in this
 * codebase as of this writing).
 *
 * Unlike the back button, placement is NOT fully fixed: callers supply
 * their own rect, since some screens (Job Create) always overflow their
 * scroll region and show these unconditionally, while others (Open Job,
 * Continue Project) only show them when the current entry count actually
 * overflows the visible list region -- a runtime check, not a layout
 * constant. UI_NAV_BUTTON_W/H below is a sizing convention (matches the
 * back button's own footprint, for visual/tap-target consistency) for
 * callers to build their rect from, not a fixed position.
 *
 * Same caller-supplied-on_activate convention as ui_grid_add_button() and
 * ui_grid_add_back_button() -- this helper does not call
 * ui_grid_move_focus() itself, since doing so requires the grid pointer,
 * which on_activate's (UiWidget *self, void *screen_ctx) signature does
 * not provide (only the screen's own ctx does, via its &ctx->grid member).
 * Every screen's own on_nav_up()/on_nav_down() callback should call
 * ui_grid_move_focus(&ctx->grid, UI_EVENT_NAV_UP/DOWN) directly -- see
 * each screen's own on_nav_up()/on_nav_down() for the one-line pattern,
 * same spirit as on_back()'s ui_stack_dispatch_event() one-liner.
 * -------------------------------------------------------------------------- */

#define UI_NAV_BUTTON_W 70
#define UI_NAV_BUTTON_H 28

/**
 * Add a single "Up" button at the given rect. Caller picks the rect (see
 * file-level doc comment above for why) and supplies on_activate, which
 * should call ui_grid_move_focus(&ctx->grid, UI_EVENT_NAV_UP).
 */
UiWidget *ui_grid_add_nav_up_button(UiWidgetGrid *grid, UiRect rect,
                                    void (*on_activate)(UiWidget *self, void *screen_ctx));

/**
 * Add a single "Down" button at the given rect. Caller picks the rect (see
 * file-level doc comment above for why) and supplies on_activate, which
 * should call ui_grid_move_focus(&ctx->grid, UI_EVENT_NAV_DOWN).
 */
UiWidget *ui_grid_add_nav_down_button(UiWidgetGrid *grid, UiRect rect,
                                      void (*on_activate)(UiWidget *self, void *screen_ctx));

/**
 * The rect a widget actually renders/hit-tests at: its literal rect,
 * shifted up by grid->scroll_y if the widget is scrollable (see
 * UiWidgetGrid::scroll_region's doc comment), unchanged otherwise. Used
 * internally by ui_grid_hit_test() and ui_grid_render(); exposed publicly
 * since both widget.c and widget_draw.c need it and it has no display.h
 * dependency of its own, so it's no different from ui_widget_rect_contains()
 * in that respect.
 */
UiRect ui_widget_effective_rect(const UiWidget *w, const UiWidgetGrid *grid);

/**
 * Mark an already-added widget as belonging to the grid's scroll region
 * (see UiWidgetGrid::scroll_region's doc comment) -- call right after the
 * matching ui_grid_add_*() call, e.g.:
 *     ui_widget_mark_scrollable(ui_grid_add_text_field(&grid, r, "Job Name", buf, cap));
 * One function rather than a _scrollable variant of every ui_grid_add_*()
 * call, so the existing five constructors stay untouched and every
 * existing caller is unaffected. Safe to call with NULL (e.g. if the
 * matching ui_grid_add_*() call failed because the grid was full) -- a
 * no-op in that case, same null-tolerance ui_grid_add_*() itself has at
 * call sites that check for a NULL return.
 */
void ui_widget_mark_scrollable(UiWidget *w);

/**
 * Define the grid's scrollable viewport in screen pixels. Only widgets
 * marked scrollable (see ui_widget_mark_scrollable()) are affected by
 * scroll_y or clipped against this region; everything else (fixed
 * chrome) renders at its literal rect regardless. Pass an all-zero UiRect
 * (the ui_grid_init() default) to disable scrolling entirely -- this is
 * the default, so grids that never call this function behave exactly as
 * before scroll support was added.
 */
void ui_grid_set_scroll_region(UiWidgetGrid *grid, UiRect region);

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
 *
 * Scoring uses each widget's literal (unscrolled) rect, so focus order
 * never changes just because the view has scrolled -- only which widgets
 * are currently visible changes. If scrolling is enabled (scroll_region
 * has non-zero area) and moving focus would land on a scrollable widget
 * outside the visible region, ui_grid_move_focus() advances scroll_y by
 * one widget-row's worth so the newly-focused widget becomes visible,
 * rather than leaving focus on an off-screen widget the person can't see.
 */
bool ui_grid_move_focus(UiWidgetGrid *grid, UiEventType dir);

/**
 * Index of the focusable widget at (x, y), or -1 if none.
 *
 * Hit-tests against each widget's effective (scrolled) on-screen
 * position -- scrollable widgets are tested at rect.y - grid->scroll_y,
 * exactly where they're actually drawn, not their literal stored rect.
 * A tap therefore always hits whatever is visually under it, scrolled or
 * not.
 */
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