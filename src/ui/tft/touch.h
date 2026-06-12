/**
 * @file touch.h
 * @brief XPT2046 resistive touch controller via Linux spidev.
 *
 * Hardware: XPT2046 on Hosyond MSP4022 (confirmed from listing spec sheet).
 *
 * Wiring (Pi Zero 2 W):
 *   T_CLK → Pin 23 (GPIO 11, shared SPI0 CLK)
 *   T_CS  → Pin 26 (GPIO 7,  SPI0 CE1, /dev/spidev0.1)
 *   T_DIN → Pin 19 (GPIO 10, shared SPI0 MOSI)
 *   T_DO  → Pin 21 (GPIO 9,  shared SPI0 MISO)
 *   T_IRQ → Pin 16 (GPIO 23, sysfs GPIO input)
 *
 * The XPT2046 uses SPI mode 0, max 2.5 MHz.
 * Raw ADC values are 12-bit (0–4095).  Caller is responsible for
 * mapping to screen coordinates via calibration constants.
 */

#ifndef GEOMARK_TOUCH_H
#define GEOMARK_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "geomark.h"

/* -------------------------------------------------------------------------
 * Calibration constants
 * Defaults approximate a typical XPT2046 on this module; override in
 * config after running a calibration pass on real hardware.
 * ---------------------------------------------------------------------- */

#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

typedef struct {
    uint16_t x; /* Screen X, 0 = left  (0 to TFT_WIDTH-1)  */
    uint16_t y; /* Screen Y, 0 = top   (0 to TFT_HEIGHT-1) */
    uint16_t z; /* Pressure (0 = no touch, higher = harder press) */
} gm_touch_point_t;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Open the XPT2046 SPI device and configure the IRQ GPIO input.
 *
 * @param spi_device  spidev path, e.g. "/dev/spidev0.1".
 * @param irq_gpio    GPIO number for T_IRQ pin (e.g. 23).
 * @return GM_OK on success, GM_ERR_IO on failure.
 */
gm_status_t touch_open(const char *spi_device, int irq_gpio);

/**
 * @brief Read the current touch state.
 *
 * Performs an 8-sample averaged read of X, Y, and Z pressure.
 * Populates @p out and returns true if a touch is detected (Z above
 * threshold), false if the screen is not being touched.
 *
 * Non-blocking: returns false immediately if T_IRQ is high.
 *
 * @param out  Destination for touch coordinates and pressure.
 * @return     true if touch detected and coordinates are valid.
 */
bool touch_read(gm_touch_point_t *out);

/**
 * @brief Close the touch SPI fd and release GPIO resources.
 *
 * Safe to call on an already-closed controller.
 */
void touch_close(void);

#endif /* GEOMARK_TOUCH_H */