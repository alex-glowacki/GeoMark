/**
 * @file display.h
 * @brief ST7796S SPI TFT display driver via Linux spidev.
 *
 * Hardware: Hosyond 4.0" 480x320 ST7796S, model MSP4022.
 * Interface: 4-wire SPI (CPOL=0, CPHA=0), DC and RST driven as sysfs GPIOs.
 *
 * Wiring (Pi Zero 2 W):
 *   VCC   → Pin 1  (3.3V)
 *   GND   → Pin 6  (GND)
 *   CS    → Pin 24 (GPIO 8,  SPI0 CE0, /dev/spidev0.0)
 *   RESET → Pin 22 (GPIO 25, sysfs GPIO output)
 *   DC/RS → Pin 18 (GPIO 24, sysfs GPIO output)
 *   MOSI  → Pin 19 (GPIO 10, SPI0 MOSI)
 *   SCK   → Pin 23 (GPIO 11, SPI0 CLK)
 *   MISO  → Pin 21 (GPIO 9,  SPI0 MISO — wire but unused for write-only)
 *   LED   → Pin 1  (3.3V — always on)
 *
 * No level shifter needed: module accepts 3.3V logic directly.
 * Pixel format: RGB565 (16 bits per pixel).
 */

#ifndef GEOMARK_DISPLAY_H
#define GEOMARK_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "geomark.h"

/* -------------------------------------------------------------------------
 * Dimensions
 * ---------------------------------------------------------------------- */

#define TFT_WIDTH 480
#define TFT_HEIGHT 320

/* -------------------------------------------------------------------------
 * RGB565 color constants
 * ---------------------------------------------------------------------- */

#define TFT_BLACK 0x0000u
#define TFT_WHITE 0xFFFFu
#define TFT_RED 0xF800u
#define TFT_GREEN 0x07E0u
#define TFT_BLUE 0x001Fu
#define TFT_YELLOW 0xFFE0u
#define TFT_CYAN 0x07FFu
#define TFT_MAGENTA 0xF81Fu
#define TFT_ORANGE 0xFD20u
#define TFT_GRAY 0x8410u
#define TFT_DKGRAY 0x4208u

/* -------------------------------------------------------------------------
 * Text scale
 * ---------------------------------------------------------------------- */

/** Base glyph size from the built-in 5x7 bitmap font, in pixels. */
#define TFT_FONT_W 5
#define TFT_FONT_H 7

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Open spidev, configure SPI, assert RST, run ST7796S init sequence.
 *
 * Combines open + hardware reset + full init in one call.  Returns GM_OK
 * only when the display is ready to accept pixel data.
 *
 * @param spi_device  spidev path, e.g. "/dev/spidev0.0".
 * @param dc_gpio     GPIO number for DC/RS pin (e.g. 24).
 * @param rst_gpio    GPIO number for RESET pin (e.g. 25).
 * @return GM_OK on success, GM_ERR_IO on any spidev or GPIO failure.
 */
gm_status_t display_open(const char *spi_device, int dc_gpio, int rst_gpio);

/**
 * @brief Flood-fill the entire display with one color.
 *
 * @param color  RGB565 color value.
 */
void display_fill(uint16_t color);

/**
 * @brief Draw a single pixel.
 *
 * Coordinates are clamped; out-of-bounds writes are silently dropped.
 *
 * @param x, y   Pixel coordinates (0-based, origin top-left).
 * @param color  RGB565 color value.
 */
void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Fill a rectangular region with one color.
 *
 * @param x, y   Top-left corner (0-based).
 * @param w, h   Width and height in pixels.
 * @param color  RGB565 color value.
 */
void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw a single ASCII character from the built-in 5x7 bitmap font.
 *
 * @param x, y   Top-left corner of the glyph cell.
 * @param c      ASCII character (printable range 0x20–0x7E; others → space).
 * @param fg     Foreground (glyph) color, RGB565.
 * @param bg     Background color, RGB565.
 * @param scale  Integer scale factor (1 = 5x7 px, 2 = 10x14 px, etc.).
 */
void display_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief Draw a null-terminated ASCII string using the built-in font.
 *
 * Characters are drawn left-to-right.  No word wrap.
 *
 * @param x, y   Top-left corner of the first glyph.
 * @param s      Null-terminated string.
 * @param fg     Foreground color, RGB565.
 * @param bg     Background color, RGB565.
 * @param scale  Integer scale factor.
 */
void display_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg,
                         uint8_t scale);

/**
 * @brief Close the spidev fd and release GPIO resources.
 *
 * Safe to call on an already-closed display.
 */
void display_close(void);

#endif /* GEOMARK_DISPLAY_H */