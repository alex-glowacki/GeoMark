/**
 * @file touch_input.h
 * @brief evdev multitouch input for the Hosyond 7" DSI capacitive panel.
 *
 * Replaces ui/tft/touch.h (XPT2046 resistive, SPI-polled) -- deleted along
 * with the SPI display backend it was coupled to. This module has zero
 * dependency on ui/tft/display.h or any SPI/GPIO code; it just opens a
 * standard Linux evdev input device node and reads ABS_MT_POSITION_X/Y
 * packets, which is why it lives in ui/core rather than ui/tft.
 *
 * The exact /dev/input/eventN node is not fixed in advance -- it depends
 * on enumeration order and varies by touch driver (ft5x06, goodix, etc).
 * touch_input_open() scans /dev/input/event* at runtime and opens the
 * first device that reports ABS_MT_POSITION_X capability, rather than
 * hardcoding a guessed node path.
 *
 * Produces UiEvent{type: UI_EVENT_TAP, x, y} directly -- per
 * ui_event.h's header comment, the future evdev driver was anticipated to
 * skip the legacy InputEvent bridge entirely and feed UiEvent straight
 * into the screen stack alongside button events.
 */

#ifndef GEOMARK_UI_CORE_TOUCH_INPUT_H
#define GEOMARK_UI_CORE_TOUCH_INPUT_H

#include <stdbool.h>

#include "geomark.h"
#include "ui/core/ui_event.h"

/**
 * @brief Scan /dev/input/event* for a multitouch capacitive touchscreen
 *        device and open it.
 *
 * @return GM_OK on success, GM_ERR_IO if no matching device is found or
 *         the open fails.
 */
gm_status_t touch_input_open(void);

/**
 * @brief Drain all pending evdev events and report at most one resolved
 *        tap.
 *
 * Non-blocking. A "tap" is reported on touch release (BTN_TOUCH going
 * from down to up) using the last known ABS_MT_POSITION_X/Y for that
 * contact, which matches how the widget grid expects a single discrete
 * UI_EVENT_TAP per physical tap rather than a stream of contact-down
 * events.
 *
 * @param out  Destination event. Only written when this function returns
 *             true; type is always UI_EVENT_TAP on a true return.
 * @return     true if a tap was resolved this call, false otherwise.
 */
bool touch_input_poll(UiEvent *out);

/**
 * @brief Close the evdev device fd.
 *
 * Safe to call on an already-closed or never-opened controller.
 */
void touch_input_close(void);

#endif /* GEOMARK_UI_CORE_TOUCH_INPUT_H */