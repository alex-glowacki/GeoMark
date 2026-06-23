/**
 * @file ui/core/ui_event.h
 * @brief Unified input event for the new widget/screen-stack UI layer.
 *
 * Distinct from the legacy ui/input.h (button-only, no coordinates) still
 * used by ui/survey_screen.c. This type adds touch-tap support with raw
 * coordinates, per the Item 11 tap-to-select decision: a tap is resolved to
 * a widget rect by the widget grid, which relocates focus and fires the
 * same activation path a button-driven UI_EVENT_ACTIVATE would use. There
 * is no parallel touch-only code path — see widget.c's
 * ui_grid_handle_event().
 *
 * ui/preview.c's screen-stack UI is touch-only: it no longer reads the
 * physical GPIO d-pad at all, so the translate_input() bridge this
 * comment used to describe is gone, and UI_EVENT_NAV_UP/DOWN/LEFT/RIGHT
 * and UI_EVENT_ACTIVATE are never produced there in practice. They
 * remain part of this enum because the legacy ui/client.c flow's own
 * button-only survey screen still exists and is reachable without
 * --ui-preview, and because ui/core/widget.c's grid logic (focus
 * movement, ACTIVATE handling) is input-source-agnostic and still
 * exercised directly by tests/test_widget.c. The evdev touch driver
 * (ui/core/touch_input.c) produces UiEvent{UI_EVENT_TAP, x, y}
 * directly -- the only event type ui/preview.c's input loop actually
 * feeds into the stack now.
 */

#ifndef GEOMARK_UI_CORE_EVENT_H
#define GEOMARK_UI_CORE_EVENT_H

#include <stdint.h>

typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_NAV_UP,
    UI_EVENT_NAV_DOWN,
    UI_EVENT_NAV_LEFT,
    UI_EVENT_NAV_RIGHT,
    UI_EVENT_ACTIVATE, /* Center button, or Enteron the focused widget */
    UI_EVENT_BACK,     /* Left-at-edge (screen's choice), or a dedicated Back action */
    UI_EVENT_TAP,      /* Raw touch tap — x, y valid, hit-tested by the widget grid */
} UiEventType;

typedef struct {
    UiEventType type;
    uint16_t x; /* valid only when type == UI_EVENT_TAP */
    uint16_t y; /* valid only when type == UI_EVENT_TAP */
} UiEvent;

#endif /* GEOMARK_UI_CORE_EVENT_H */