/**
 * @file display.c
 * @brief ST7796S SPI display driver via Linux spidev.
 *
 * GPIO via kernel character device API (/dev/gpiochip0,
 * GPIO_V2_GET_LINE_IOCTL). Required on kernel 6.x — sysfs GPIO deprecated.
 *
 * MADCTL 0x28 (MV|MY) confirmed on hardware for full-screen landscape fill.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>

#include "ui/tft/display.h"
#include "util/log.h"

/* -------------------------------------------------------------------------
 * ST7796S command bytes
 * ---------------------------------------------------------------------- */

#define ST7796_SLPOUT   0x11
#define ST7796_NORON    0x13
#define ST7796_DISPON   0x29
#define ST7796_CASET    0x2A
#define ST7796_RASET    0x2B
#define ST7796_RAMWR    0x2C
#define ST7796_MADCTL   0x36
#define ST7796_COLMOD   0x3A
#define ST7796_B4H      0xB4
#define ST7796_B6H      0xB6
#define ST7796_C1H      0xC1
#define ST7796_C2H      0xC2
#define ST7796_C5H      0xC5
#define ST7796_E0H      0xE0
#define ST7796_E1H      0xE1
#define ST7796_E8H      0xE8
#define ST7796_F0H      0xF0

/*
 * MADCTL 0x28 = MV|MY — confirmed on hardware (Hosyond MSP4022).
 * MV=1: row/col exchange (landscape), MY=1: Y-mirror.
 * 0x48 (MV|MX) fills only right 2/3 of screen on this module.
 */
#define ST7796_MADCTL_LANDSCAPE 0x28

/* COLMOD 0x55 = RGB565 */
#define ST7796_COLMOD_16BIT 0x55

/* SPI clock: 40 MHz */
#define TFT_SPI_HZ 40000000u

/* gpiochip device */
#define TFT_GPIOCHIP "/dev/gpiochip0"

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static int s_spi_fd   = -1;
static int s_gpio_fd  = -1;  /* /dev/gpiochip0 chip fd */
static int s_dc_fd    = -1;  /* line request fd — DC/RS */
static int s_rst_fd   = -1;  /* line request fd — RESET */

/* -------------------------------------------------------------------------
 * GPIO character device helpers
 * ---------------------------------------------------------------------- */

static int gpio_request_output(int chip_fd, unsigned int line, int initial)
{
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));

    req.offsets[0] = line;
    req.num_lines  = 1;
    snprintf(req.consumer, sizeof(req.consumer), "geomark-display");

    req.config.flags     = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id     = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = (uint64_t)initial;
    req.config.attrs[0].mask        = 1ULL;

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        log_error("display: gpio request line %u: %s", line, strerror(errno));
        return -1;
    }
    return req.fd;
}

static void gpio_set(int line_fd, int value)
{
    struct gpio_v2_line_values vals;
    memset(&vals, 0, sizeof(vals));
    vals.mask = 1ULL;
    vals.bits = (uint64_t)(value ? 1 : 0);
    if (ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0)
        log_error("display: gpio_set: %s", strerror(errno));
}

/* -------------------------------------------------------------------------
 * Low-level SPI helpers
 * ---------------------------------------------------------------------- */

static void tft_write_cmd(uint8_t cmd)
{
    gpio_set(s_dc_fd, 0);
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)&cmd,
        .rx_buf        = 0,
        .len           = 1,
        .speed_hz      = TFT_SPI_HZ,
        .bits_per_word = 8,
        .delay_usecs   = 0,
    };
    if (ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0)
        log_error("display: SPI cmd 0x%02X: %s", cmd, strerror(errno));
}

static void tft_write_data(uint8_t data)
{
    gpio_set(s_dc_fd, 1);
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)&data,
        .rx_buf        = 0,
        .len           = 1,
        .speed_hz      = TFT_SPI_HZ,
        .bits_per_word = 8,
        .delay_usecs   = 0,
    };
    if (ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0)
        log_error("display: SPI data 0x%02X: %s", data, strerror(errno));
}

static void tft_write_buf(const uint8_t *buf, size_t len)
{
    gpio_set(s_dc_fd, 1);
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)buf,
        .rx_buf        = 0,
        .len           = (uint32_t)len,
        .speed_hz      = TFT_SPI_HZ,
        .bits_per_word = 8,
        .delay_usecs   = 0,
    };
    if (ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0)
        log_error("display: SPI buf write: %s", strerror(errno));
}

/* -------------------------------------------------------------------------
 * Address window
 * ---------------------------------------------------------------------- */

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    tft_write_cmd(ST7796_CASET);
    uint8_t ca[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
    };
    tft_write_buf(ca, 4);

    tft_write_cmd(ST7796_RASET);
    uint8_t ra[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
    };
    tft_write_buf(ra, 4);

    tft_write_cmd(ST7796_RAMWR);
}

/* -------------------------------------------------------------------------
 * ST7796S init sequence
 * ---------------------------------------------------------------------- */

static void tft_hw_reset(void)
{
    gpio_set(s_rst_fd, 1); usleep(10000);
    gpio_set(s_rst_fd, 0); usleep(10000);
    gpio_set(s_rst_fd, 1); usleep(120000);
}

static void tft_init_sequence(void)
{
    tft_write_cmd(ST7796_SLPOUT); usleep(120000);

    tft_write_cmd(ST7796_F0H); tft_write_data(0xC3);
    tft_write_cmd(ST7796_F0H); tft_write_data(0x96);

    tft_write_cmd(ST7796_MADCTL); tft_write_data(ST7796_MADCTL_LANDSCAPE);
    tft_write_cmd(ST7796_COLMOD); tft_write_data(ST7796_COLMOD_16BIT);

    tft_write_cmd(ST7796_B4H); tft_write_data(0x01);

    tft_write_cmd(ST7796_B6H);
    tft_write_data(0x80); tft_write_data(0x02); tft_write_data(0x3B);

    tft_write_cmd(ST7796_E8H);
    tft_write_data(0x40); tft_write_data(0x8A); tft_write_data(0x00);
    tft_write_data(0x00); tft_write_data(0x29); tft_write_data(0x19);
    tft_write_data(0xA5); tft_write_data(0x33);

    tft_write_cmd(ST7796_C1H); tft_write_data(0x06);
    tft_write_cmd(ST7796_C2H); tft_write_data(0xA7);
    tft_write_cmd(ST7796_C5H); tft_write_data(0x18);

    tft_write_cmd(ST7796_E0H);
    tft_write_data(0x0F); tft_write_data(0x09); tft_write_data(0x0B);
    tft_write_data(0x06); tft_write_data(0x04); tft_write_data(0x15);
    tft_write_data(0x2F); tft_write_data(0x54); tft_write_data(0x42);
    tft_write_data(0x3C); tft_write_data(0x17); tft_write_data(0x14);
    tft_write_data(0x18); tft_write_data(0x1B); tft_write_data(0x00);

    tft_write_cmd(ST7796_E1H);
    tft_write_data(0x0F); tft_write_data(0x09); tft_write_data(0x0B);
    tft_write_data(0x06); tft_write_data(0x04); tft_write_data(0x03);
    tft_write_data(0x2D); tft_write_data(0x43); tft_write_data(0x42);
    tft_write_data(0x3B); tft_write_data(0x16); tft_write_data(0x14);
    tft_write_data(0x17); tft_write_data(0x1B); tft_write_data(0x00);

    tft_write_cmd(ST7796_F0H); tft_write_data(0xC3);
    tft_write_cmd(ST7796_F0H); tft_write_data(0x96);

    tft_write_cmd(ST7796_NORON);  usleep(10000);
    tft_write_cmd(ST7796_DISPON); usleep(20000);
}

/* -------------------------------------------------------------------------
 * Built-in 5x7 bitmap font (ASCII 0x20–0x7E)
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

gm_status_t display_open(const char *spi_device, int dc_gpio, int rst_gpio)
{
    /* Open gpiochip0 */
    s_gpio_fd = open(TFT_GPIOCHIP, O_RDONLY);
    if (s_gpio_fd < 0) {
        log_error("display: open %s: %s", TFT_GPIOCHIP, strerror(errno));
        return GM_ERR_IO;
    }

    /* Request DC and RST as outputs, initially high */
    s_dc_fd  = gpio_request_output(s_gpio_fd, (unsigned int)dc_gpio,  1);
    s_rst_fd = gpio_request_output(s_gpio_fd, (unsigned int)rst_gpio, 1);
    if (s_dc_fd < 0 || s_rst_fd < 0) {
        log_error("display: failed to acquire GPIO lines");
        close(s_gpio_fd); s_gpio_fd = -1;
        return GM_ERR_IO;
    }

    /* Open spidev */
    s_spi_fd = open(spi_device, O_RDWR);
    if (s_spi_fd < 0) {
        log_error("display: open %s: %s", spi_device, strerror(errno));
        close(s_dc_fd);  s_dc_fd  = -1;
        close(s_rst_fd); s_rst_fd = -1;
        close(s_gpio_fd); s_gpio_fd = -1;
        return GM_ERR_IO;
    }

    uint8_t mode = SPI_MODE_0;
    if (ioctl(s_spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        log_error("display: SPI_IOC_WR_MODE: %s", strerror(errno));
        goto err;
    }
    uint8_t bits = 8;
    if (ioctl(s_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        log_error("display: SPI_IOC_WR_BITS_PER_WORD: %s", strerror(errno));
        goto err;
    }
    uint32_t speed = TFT_SPI_HZ;
    if (ioctl(s_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        log_error("display: SPI_IOC_WR_MAX_SPEED_HZ: %s", strerror(errno));
        goto err;
    }

    tft_hw_reset();
    tft_init_sequence();
    display_fill(TFT_BLACK);

    log_info("display: ST7796S ready on %s (DC=%d RST=%d)",
             spi_device, dc_gpio, rst_gpio);
    return GM_OK;

err:
    close(s_spi_fd);  s_spi_fd  = -1;
    close(s_dc_fd);   s_dc_fd   = -1;
    close(s_rst_fd);  s_rst_fd  = -1;
    close(s_gpio_fd); s_gpio_fd = -1;
    return GM_ERR_IO;
}

void display_fill(uint16_t color)
{
    if (s_spi_fd < 0) return;

    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    uint32_t stride = TFT_WIDTH * 2u;
    uint8_t *line = malloc(stride);
    if (!line) { log_error("display: fill: malloc failed"); return; }

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (uint32_t i = 0; i < stride; i += 2) {
        line[i]     = hi;
        line[i + 1] = lo;
    }

    gpio_set(s_dc_fd, 1);
    for (uint16_t row = 0; row < TFT_HEIGHT; row++) {
        struct spi_ioc_transfer tr = {
            .tx_buf        = (unsigned long)line,
            .rx_buf        = 0,
            .len           = stride,
            .speed_hz      = TFT_SPI_HZ,
            .bits_per_word = 8,
            .delay_usecs   = 0,
        };
        if (ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            log_error("display: fill row %d: %s", row, strerror(errno));
            break;
        }
    }
    free(line);
}

void display_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (s_spi_fd < 0 || x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    tft_set_window(x, y, x, y);
    uint8_t px[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    tft_write_buf(px, 2);
}

void display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color)
{
    if (s_spi_fd < 0 || w == 0 || h == 0) return;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = (uint16_t)(TFT_WIDTH  - x);
    if (y + h > TFT_HEIGHT) h = (uint16_t)(TFT_HEIGHT - y);

    tft_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    uint32_t stride = (uint32_t)w * 2u;
    uint8_t *line = malloc(stride);
    if (!line) return;

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (uint32_t i = 0; i < stride; i += 2) {
        line[i]     = hi;
        line[i + 1] = lo;
    }

    gpio_set(s_dc_fd, 1);
    for (uint16_t row = 0; row < h; row++) {
        struct spi_ioc_transfer tr = {
            .tx_buf        = (unsigned long)line,
            .rx_buf        = 0,
            .len           = stride,
            .speed_hz      = TFT_SPI_HZ,
            .bits_per_word = 8,
            .delay_usecs   = 0,
        };
        ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr);
    }
    free(line);
}

void display_draw_char(uint16_t x, uint16_t y, char c,
                       uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (s_spi_fd < 0 || scale == 0) return;

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

void display_close(void)
{
    if (s_spi_fd  >= 0) { close(s_spi_fd);  s_spi_fd  = -1; }
    if (s_rst_fd  >= 0) { close(s_rst_fd);  s_rst_fd  = -1; }
    if (s_dc_fd   >= 0) { close(s_dc_fd);   s_dc_fd   = -1; }
    if (s_gpio_fd >= 0) { close(s_gpio_fd); s_gpio_fd = -1; }
    log_info("display: closed");
}