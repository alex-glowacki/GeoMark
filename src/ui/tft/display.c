/**
 * @file display.c
 * @brief Framebuffer display driver for the Hosyond 7" IPS DSI panel.
 *
 * All draw_* calls write only into the off-screen backbuffer (s_backbuf).
 * Nothing reaches the physical panel until display_present() does one
 * memcpy into the mmap'd /dev/fb0 region -- the panel never displays a
 * partially-drawn frame, which is what produced the old SPI driver's
 * visible flicker (every display_fill_rect()/draw_char() call there was
 * its own slow, independently-visible SPI transaction).
 *
 * Pixel format is read from the kernel at display_open() time rather than
 * assumed, since the spec sheet and the kernel's actual reported mode can
 * differ (e.g. RGB888 panel interface vs. a 16bpp or 32bpp in-memory
 * framebuffer format) -- this is the runtime equivalent of this project's
 * "diagnose against real hardware" rule, applied here because the panel
 * had not arrived yet when this file was written.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "ui/tft/display.h"
#include "util/log.h"

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static int       s_fb_fd        = -1;
static uint8_t  *s_fb_mem       = NULL;  /* mmap'd /dev/fb0                */
static size_t    s_fb_mmap_len  = 0;
static uint16_t *s_backbuf      = NULL;  /* RGB565, TFT_WIDTH * TFT_HEIGHT */

static struct fb_var_screeninfo s_vinfo;
static struct fb_fix_screeninfo s_finfo;

/* -------------------------------------------------------------------------
 * Backbuffer pixel access
 * ---------------------------------------------------------------------- */

static inline void backbuf_set(uint16_t x, uint16_t y, uint16_t color)
{
    s_backbuf[(uint32_t)y * TFT_WIDTH + x] = color;
}

/* -------------------------------------------------------------------------
 * RGB565 -> native framebuffer format conversion
 * ---------------------------------------------------------------------- */

static inline uint8_t rgb565_r8(uint16_t c) { return (uint8_t)(((c >> 11) & 0x1F) * 255 / 31); }
static inline uint8_t rgb565_g8(uint16_t c) { return (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63); }
static inline uint8_t rgb565_b8(uint16_t c) { return (uint8_t)((c        & 0x1F) * 255 / 31); }

/**
 * @brief Write one backbuffer pixel into the mmap'd framebuffer at byte
 *        offset `off`, converting from RGB565 to whatever format
 *        s_vinfo reported (16bpp RGB565 passthrough, or 24/32bpp
 *        RGB888/XRGB8888 expansion).
 */
static inline void fb_write_pixel(uint8_t *dst, uint16_t color)
{
    switch (s_vinfo.bits_per_pixel) {
    case 16:
        /* Assume the common RGB565 16bpp layout -- matches our own type. */
        dst[0] = (uint8_t)(color & 0xFF);
        dst[1] = (uint8_t)(color >> 8);
        break;

    case 24:
        /* RGB888, byte order per offsets reported by the kernel. */
        dst[s_vinfo.red.offset   / 8] = rgb565_r8(color);
        dst[s_vinfo.green.offset / 8] = rgb565_g8(color);
        dst[s_vinfo.blue.offset  / 8] = rgb565_b8(color);
        break;

    case 32:
    default:
        /* XRGB8888 / ARGB8888 -- pad/alpha byte left untouched (0). */
        dst[s_vinfo.red.offset   / 8] = rgb565_r8(color);
        dst[s_vinfo.green.offset / 8] = rgb565_g8(color);
        dst[s_vinfo.blue.offset  / 8] = rgb565_b8(color);
        break;
    }
}

/* -------------------------------------------------------------------------
 * Built-in 5x7 bitmap font (ASCII 0x20-0x7E)
 * ---------------------------------------------------------------------- */

static const uint8_t s_font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 space */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 ! */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22 " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 $ */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25 % */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26 & */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27 ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A * */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B + */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C , */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D - */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E . */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A : */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ; */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C < */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D = */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E > */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 A */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 B */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 D */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 E */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 H */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 I */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A J */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B K */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F O */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 R */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53 S */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 W */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58 X */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59 Y */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B [ */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ] */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F _ */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60 ` */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61 a */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 b */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63 c */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 d */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65 e */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 g */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 h */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 i */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A j */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B k */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C l */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D m */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E n */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F o */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 p */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 q */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 r */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73 s */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 t */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 0x75 u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 w */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78 x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79 y */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A z */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B { */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C | */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D } */
    {0x10,0x08,0x08,0x10,0x08}, /* 0x7E ~ */
};

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

gm_status_t display_open(const char *fb_device)
{
    s_fb_fd = open(fb_device, O_RDWR);
    if (s_fb_fd < 0) {
        log_error("display: open %s: %s", fb_device, strerror(errno));
        return GM_ERR_IO;
    }

    if (ioctl(s_fb_fd, FBIOGET_VSCREENINFO, &s_vinfo) < 0) {
        log_error("display: FBIOGET_VSCREENINFO: %s", strerror(errno));
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_IO;
    }
    if (ioctl(s_fb_fd, FBIOGET_FSCREENINFO, &s_finfo) < 0) {
        log_error("display: FBIOGET_FSCREENINFO: %s", strerror(errno));
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_IO;
    }

    log_info("display: fb reports %ux%u, %ubpp, line_length=%u",
             s_vinfo.xres, s_vinfo.yres, s_vinfo.bits_per_pixel,
             s_finfo.line_length);

    if (s_vinfo.xres < TFT_WIDTH || s_vinfo.yres < TFT_HEIGHT) {
        log_error("display: fb geometry %ux%u smaller than expected %dx%d",
                  s_vinfo.xres, s_vinfo.yres, TFT_WIDTH, TFT_HEIGHT);
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_IO;
    }
    if (s_vinfo.bits_per_pixel != 16 &&
        s_vinfo.bits_per_pixel != 24 &&
        s_vinfo.bits_per_pixel != 32) {
        log_error("display: unsupported fb bpp %u", s_vinfo.bits_per_pixel);
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_IO;
    }

    s_fb_mmap_len = (size_t)s_finfo.line_length * s_vinfo.yres;
    s_fb_mem = mmap(NULL, s_fb_mmap_len, PROT_READ | PROT_WRITE,
                    MAP_SHARED, s_fb_fd, 0);
    if (s_fb_mem == MAP_FAILED) {
        log_error("display: mmap: %s", strerror(errno));
        s_fb_mem = NULL;
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_IO;
    }

    s_backbuf = malloc((size_t)TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t));
    if (!s_backbuf) {
        log_error("display: backbuffer malloc failed");
        munmap(s_fb_mem, s_fb_mmap_len); s_fb_mem = NULL;
        close(s_fb_fd); s_fb_fd = -1;
        return GM_ERR_NOMEM;
    }

    display_fill(TFT_BLACK);
    display_present();

    log_info("display: fbdev ready on %s (%dx%d backbuffer)",
             fb_device, TFT_WIDTH, TFT_HEIGHT);
    return GM_OK;
}

void display_fill(uint16_t color)
{
    if (!s_backbuf) return;
    for (uint32_t i = 0; i < (uint32_t)TFT_WIDTH * TFT_HEIGHT; i++)
        s_backbuf[i] = color;
}

void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!s_backbuf || x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    backbuf_set(x, y, color);
}

void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color)
{
    if (!s_backbuf || w == 0 || h == 0) return;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = (uint16_t)(TFT_WIDTH  - x);
    if (y + h > TFT_HEIGHT) h = (uint16_t)(TFT_HEIGHT - y);

    for (uint16_t row = 0; row < h; row++)
        for (uint16_t col = 0; col < w; col++)
            backbuf_set((uint16_t)(x + col), (uint16_t)(y + row), color);
}

void display_draw_char(uint16_t x, uint16_t y, char c,
                       uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (!s_backbuf || scale == 0) return;

    uint8_t idx = (uint8_t)c;
    if (idx < 0x20 || idx > 0x7E) idx = 0x20;
    const uint8_t *glyph = s_font5x7[idx - 0x20];

    for (uint8_t col = 0; col < TFT_FONT_W; col++) {
        uint8_t coldata = glyph[col];
        for (uint8_t row = 0; row < TFT_FONT_H; row++) {
            uint16_t color = (coldata & (1u << row)) ? fg : bg;
            display_fill_rect(
                (uint16_t)(x + col * scale),
                (uint16_t)(y + row * scale),
                scale, scale, color);
        }
    }
}

void display_draw_string(uint16_t x, uint16_t y, const char *s,
                         uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (!s || scale == 0) return;
    uint16_t cx = x;
    while (*s) {
        display_draw_char(cx, y, *s, fg, bg, scale);
        cx = (uint16_t)(cx + (TFT_FONT_W + 1) * scale);
        s++;
    }
}

void display_present(void)
{
    if (!s_backbuf || !s_fb_mem) return;

    uint32_t bytes_per_px = s_vinfo.bits_per_pixel / 8;

    for (uint16_t y = 0; y < TFT_HEIGHT; y++) {
        uint8_t *dst_row = s_fb_mem + (size_t)y * s_finfo.line_length;
        for (uint16_t x = 0; x < TFT_WIDTH; x++) {
            uint16_t color = s_backbuf[(uint32_t)y * TFT_WIDTH + x];
            fb_write_pixel(dst_row + (size_t)x * bytes_per_px, color);
        }
    }
}

void display_close(void)
{
    if (s_backbuf) { free(s_backbuf); s_backbuf = NULL; }
    if (s_fb_mem)  { munmap(s_fb_mem, s_fb_mmap_len); s_fb_mem = NULL; }
    if (s_fb_fd >= 0) { close(s_fb_fd); s_fb_fd = -1; }
    log_info("display: closed");
}