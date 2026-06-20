/**
 * @file ui/core/keyboard.h
 * @brief On-screen QWERTY keyboard, embedded into a screen's own
 *        UiWidgetGrid rather than living as a separate screen or overlay.
 *
 * Design (touch-first, see geomark-ui-redesign-decisions.md and the
 * session that introduced this file for the full rationale):
 *   - Every key is an ordinary WIDGET_BUTTON added to the *same* grid as
 *     the screen's own form widgets (label, text field, Create button,
 *     etc.) — there is no second grid, no modal push onto the screen
 *     stack, and no overlay-on-top-of-a-different-grid scheme. This is
 *     what makes "touch any element while the keyboard is shown" true
 *     for free: ui_grid_handle_event()'s existing UI_EVENT_TAP hit-test
 *     already covers every widget in the grid, keyboard keys included.
 *   - Hard constraint this design works around: ui_grid_handle_event()
 *     calls every widget's on_activate with the *same* grid->screen_ctx,
 *     grid-wide, with no per-widget override (confirmed against
 *     widget.c's activate_widget()). A screen's own buttons and the
 *     keyboard's keys are both in that one grid, so they necessarily
 *     receive the same pointer. This module resolves that by requiring
 *     UiKeyboardTarget to be the *first member* of whatever ctx struct
 *     the owning screen passes to ui_grid_init() — see
 *     ui/screens/new_project_screen.h for the convention in practice.
 *     A (UiKeyboardTarget *) cast of screen_ctx is then always valid
 *     inside this module's own key callbacks, regardless of which
 *     concrete screen ctx it actually is, while the screen's own
 *     on_activate callbacks keep using their real ctx type as before
 *     (the first-member layout makes both casts of the same pointer
 *     valid simultaneously).
 *   - Each key's on_activate callback does not target itself; it mutates
 *     whichever text field the owning screen currently considers the
 *     "active edit target" (see UiKeyboardTarget below). The keyboard
 *     module has no opinion on which field that is — only the owning
 *     screen does, since a screen may have more than one text field.
 *   - D-pad reach: Up/Down/Center move focus *vertically* via the grid's
 *     existing geometric ui_grid_move_focus() — that part works today.
 *     Left/Right *within* a key row do not: Left is permanently bound to
 *     UI_EVENT_BACK at the ui/preview.c translation point (see
 *     ui/core/ui_event.h), and Right is currently unreliable hardware
 *     (Field Hardening Gameplan item 10). Touch is therefore the only
 *     fully reliable way to reach an individual key today. This is a
 *     deliberate, documented scope limit, not an oversight — revisit
 *     once Right's wiring is fixed.
 *   - Character set is closed by construction: letters (rendered
 *     uppercase to match the legacy survey_screen.c keyboard's
 *     convention), digits, '-', '_', and space. No other character can
 *     ever reach a buffer through this keyboard, which is also why no
 *     separate path-safety validation is needed wherever this keyboard
 *     feeds a value that becomes a filesystem path component (e.g. a
 *     project name).
 *   - Key labels (the single character/word ui_grid_add_button() stores
 *     as each key's `label`) are not copied by the grid -- see widget.h's
 *     ownership doc comment -- so they must outlive the widget. Rather
 *     than a module-global static table (which would silently corrupt
 *     between two screens that both embed a keyboard and are alive on
 *     the stack at once, e.g. Continue Project resuming under a still-
 *     live screen below it), the caller supplies its own UiKeyboardLabels
 *     storage, sized exactly for the 41 keys this layout produces. One
 *     instance per screen that embeds a keyboard, living as long as that
 *     screen's own ctx does.
 *
 * Layout (tuned for the 800x480 Hosyond DSI panel; key sizing borrowed
 * directly from the legacy ui/survey_screen.c keyboard, which was already
 * hand-tuned for this exact panel):
 *   Row 0 (digits):  1234567890        (10 keys)
 *   Row 1:           QWERTYUIOP        (10 keys)
 *   Row 2:           ASDFGHJKL         (9 keys)
 *   Row 3:           ZXCVBNM-_         (9 keys)
 *   Action row:      Space | Del | Done (3 keys)
 *   Total: 41 keys.
 */

#ifndef GEOMARK_UI_CORE_KEYBOARD_H
#define GEOMARK_UI_CORE_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>

#include "ui/core/widget.h"

/* -------------------------------------------------------------------------
 * Layout constants — caller needs KEYBOARD_HEIGHT to lay out the form
 * region above it; the rest are internal to keyboard.c's own key_rect().
 * ---------------------------------------------------------------------- */

#define KEYBOARD_TOP_Y                                                                             \
    248                     /* matches the legacy keyboard's KB_ACTION_Y                           \
                             * minus its three alpha rows, re-derived                              \
                             * for this module's own row math */
#define KEYBOARD_HEIGHT 232 /* TFT_HEIGHT (480) - KEYBOARD_TOP_Y */

/** Exact count of single-character keys across all four rows (10+10+9+9). */
#define KB_CHAR_KEY_COUNT 38

/**
 * Caller-owned storage for the char keys' single-character label strings.
 * One instance per screen that calls keyboard_add_to_grid() — see the
 * file-level doc comment for why this can't be a module-static table.
 * Opaque; the screen only ever passes a pointer to one of these through
 * unchanged, never reads or writes its contents directly.
 */
typedef struct {
    char slots[KB_CHAR_KEY_COUNT][2];
} UiKeyboardLabels;

/**
 * The owning screen's "active edit target" — which text field keyboard
 * keys currently write into, and what to do when Done is pressed.
 *
 * MUST be the first member of the screen's own ctx struct (see the
 * file-level doc comment above for why) — e.g.:
 *
 *     typedef struct {
 *         UiKeyboardTarget kb;   // must be first
 *         UiWidgetGrid grid;
 *         ... screen's own fields ...
 *     } MyScreenCtx;
 *
 * The screen owns and updates this struct directly (typically: point
 * kb.buf/kb.len at a field's storage when that field is activated, clear
 * them to NULL when Done fires so stray key presses with no active field
 * are silently ignored). The keyboard module only ever reads
 * buf/buf_cap/len and calls on_done; it never owns this struct's memory.
 */
typedef struct {
    char *buf; /* the focused text field's buffer; NULL = no target */
    size_t buf_cap;
    size_t *len;                       /* current string length, kept in sync by the keyboard
                                        * so the caller doesn't have to re-strlen every key */
    void (*on_done)(void *screen_ctx); /* fired when Done is pressed; may be NULL */
    void *screen_ctx;                  /* passed to on_done verbatim -- conventionally the
                                        * same screen ctx pointer that starts with this
                                        * UiKeyboardTarget, so on_done can recover the
                                        * full screen ctx via the same cast trick */
} UiKeyboardTarget;

/**
 * Add all keyboard keys to an already-initialized grid, positioned in the
 * bottom KEYBOARD_HEIGHT pixels of the panel.
 *
 * grid->screen_ctx (set by ui_grid_init()) MUST already point at a struct
 * whose first member is a UiKeyboardTarget — this function does not take
 * a separate target parameter because there is only one grid-wide
 * screen_ctx (see the file-level doc comment); it reinterprets
 * grid->screen_ctx as (UiKeyboardTarget *) internally for every key's
 * own bookkeeping, while the screen's own widgets keep receiving that
 * same pointer cast back to their real ctx type, as every other screen
 * already does.
 *
 * labels must outlive the grid (typically: a UiKeyboardLabels field
 * alongside the grid in the screen's own ctx struct) — see
 * UiKeyboardLabels's doc comment for why this can't be hidden as
 * internal static storage.
 *
 * Returns false if the grid does not have room for all 41 keys (caller
 * should treat this the same as any other ui_grid_add_* failure: a logged
 * error from widget.c, already, since this just calls ui_grid_add_button
 * in a loop).
 */
bool keyboard_add_to_grid(UiWidgetGrid *grid, UiKeyboardLabels *labels);

#endif /* GEOMARK_UI_CORE_KEYBOARD_H */