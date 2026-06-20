/**
 * @file display.h
 * @brief Framebuffer display driver for the Hosyond 7" IPS DSI panel.
 *
 * Hardware: Hosyond 7" 800x480 IPS, MIPI DSI, 5-point capacitive touch.
 * Interface: standard Linux framebuffer device (/dev/fb0). Driver-free on
 * Raspberry Pi OS -- no SPI, no GPIO DC/RST lines, no custom kernel driver.
 * Replaces the earlier 4" ST7796S SPI panel (see git history for that
 * driver if ever needed again).
 *
 * All drawing happens into an off-screen backbuffer; nothing reaches the
 * physical screen until display_present() copies the whole frame to
 * /dev/fb0 in one mmap'd memcpy. This is what eliminates the visible
 * "draw sweep" flicker the old per-call SPI driver had -- the panel never
 * shows a half-drawn frame.
 *
 * Pixel format: callers still pass RGB565 (uint16_t), matching every
 * existing color constant and every screen/widget file already written
 * against this header. display.c converts RGB565 -> whatever format the
 * kernel framebuffer actually reports (read at display_open() time via
 * FBIOGET_VSCREENINFO) at the point each pixel is written into the
 * backbuffer -- invisible to every caller.
 */

#ifndef GEOMARK_DISPLAY_H
#define GEOMARK_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "geomark.h"

/* -------------------------------------------------------------------------
 * Dimensions
 * ---------------------------------------------------------------------- */

#define TFT_WIDTH 800
#define TFT_HEIGHT 480

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
 * @brief Open the framebuffer device, query its real geometry/format, and
 *        allocate the off-screen backbuffer.
 *
 * @param fb_device  Framebuffer device path, e.g. "/dev/fb0".
 * @return GM_OK on success, GM_ERR_IO on any open/mmap/ioctl failure.
 */
gm_status_t display_open(const char *fb_device);

/**
 * @brief Flood-fill the entire backbuffer with one color.
 * @param color  RGB565 color value.
 */
void display_fill(uint16_t color);

/**
 * @brief Draw a single pixel into the backbuffer.
 * @param x, y   Pixel coordinates (0-based, origin top-left).
 * @param color  RGB565 color value.
 */
void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Fill a rectangular region of the backbuffer with one color.
 * @param x, y   Top-left corner (0-based).
 * @param w, h   Width and height in pixels.
 * @param color  RGB565 color value.
 */
void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw a single ASCII character from the built-in 5x7 bitmap font
 *        into the backbuffer.
 * @param x, y   Top-left corner of the glyph cell.
 * @param c      ASCII character (printable range 0x20-0x7E).
 * @param fg     Foreground color, RGB565.
 * @param bg     Background color, RGB565.
 * @param scale  Integer scale factor (1 = 5x7 px, 2 = 10x14 px, etc.).
 */
void display_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief Draw a null-terminated ASCII string into the backbuffer using the
 *        built-in font.
 * @param x, y   Top-left corner of the first glyph.
 * @param s      Null-terminated string.
 * @param fg     Foreground color, RGB565.
 * @param bg     Background color, RGB565.
 * @param scale  Integer scale factor.
 */
void display_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg,
                         uint8_t scale);

/**
 * @brief Copy the entire backbuffer to the physical framebuffer in one
 *        shot. Call this exactly once per rendered frame, after all
 *        display_fill/draw_* calls for that frame are done. This is the
 *        only point at which anything becomes visible on screen.
 */
void display_present(void);

/**
 * @brief Unmap the framebuffer, free the backbuffer, and close the fd.
 */
void display_close(void);

#endif /* GEOMARK_DISPLAY_H */