/**
 * @file ui/input.h
 * @brief Unified input event type for GeoMark handheld UI.
 *
 * Abstracts physical button presses into navigation and action events.
 * All UI state machines consume InputEvent — no pixel coordinates.
 */

#ifndef GEOMARK_UI_INPUT_H
#define GEOMARK_UI_INPUT_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Button identifiers — match physical d-pad layout
 * -------------------------------------------------------------------------- */

typedef enum {
    BTN_UP = 0,
    BTN_DOWN = 1,
    BTN_LEFT = 2,
    BTN_RIGHT = 3,
    BTN_CENTER = 4,
    BTN_COUNT = 5,
} ButtonId;

/* --------------------------------------------------------------------------
 * Input event
 * -------------------------------------------------------------------------- */

typedef enum {
    INPUT_NONE = 0,
    INPUT_BTN_UP,
    INPUT_BTN_DOWN,
    INPUT_BTN_LEFT,
    INPUT_BTN_RIGHT,
    INPUT_BTN_CENTER,
} InputEvent;

#endif /* GEOMARK_UI_INPUT_H */