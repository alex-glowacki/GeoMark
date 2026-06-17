/**
 * @file ui/gpio_button.h
 * @brief GPIO button polling for GeoMark handheld (Pi 5).
 *
 * Manages 5 physical buttons via Linux GPIO character device API.
 * Provides software debounce. Non-blocking poll.
 *
 * Wiring:
 *   BTN_UP     — GPIO 5  (Pin 29)
 *   BTN_DOWN   — GPIO 6  (Pin 31)
 *   BTN_LEFT   — GPIO 13 (Pin 33)
 *   BTN_RIGHT  — GPIO 19 (Pin 35)
 *   BTN_CENTER — GPIO 26 (Pin 37)
 *
 * All buttons wired active-low with internal pull-up enabled.
 */

#ifndef GEOMARK_UI_GPIO_BUTTON_H
#define GEOMARK_UI_GPIO_BUTTON_H

#include "geomark.h"
#include "ui/input.h"

/**
 * @brief Open /dev/gpiochip0 and request all 5 button lines.
 *
 * @return GM_OK on success, GM_ERR_IO on failure.
 */
gm_status_t gpio_button_open(void);

/**
 * @brief Poll all buttons and return the first pressed event.
 *
 * Non-blocking. Returns INPUT_NONE if no button is pressed or if the
 * debounce window has not elapsed since the last accepted event.
 *
 * @return InputEvent — one of INPUT_BTN_* or INPUT_NONE.
 */
InputEvent gpio_button_poll(void);

/**
 * @brief Release all GPIO lines and close the chip fd.
 */
void gpio_button_close(void);

#endif /* GEOMARK_UI_GPIO_BUTTON_H */