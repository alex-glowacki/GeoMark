/**
 * @file touch.c
 * @brief XPT2046 resistive touch controller via Linux spidev.
 *
 * GPIO via kernel character device API (/dev/gpiochip0,
 * GPIO_V2_GET_LINE_IOCTL). Required on kernel 6.x.
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

#include "ui/tft/touch.h"
#include "ui/tft/display.h"
#include "util/log.h"

/* -------------------------------------------------------------------------
 * XPT2046 command bytes
 * ---------------------------------------------------------------------- */

#define XPT_READ_X  0xD0
#define XPT_READ_Y  0x90
#define XPT_READ_Z1 0xB0
#define XPT_READ_Z2 0xC0

#define TOUCH_Z_THRESHOLD 100
#define TOUCH_SAMPLES     8
#define TOUCH_SPI_HZ      2000000u
#define TOUCH_GPIOCHIP    "/dev/gpiochip0"

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static int s_spi_fd  = -1;
static int s_gpio_fd = -1;
static int s_irq_fd  = -1;  /* line request fd for T_IRQ (input) */

/* -------------------------------------------------------------------------
 * GPIO character device helpers
 * ---------------------------------------------------------------------- */

static int gpio_request_input(int chip_fd, unsigned int line)
{
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));

    req.offsets[0] = line;
    req.num_lines  = 1;
    snprintf(req.consumer, sizeof(req.consumer), "geomark-touch");
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT
                     | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        log_error("touch: gpio request line %u: %s", line, strerror(errno));
        return -1;
    }
    return req.fd;
}

static int gpio_get(int line_fd)
{
    struct gpio_v2_line_values vals;
    memset(&vals, 0, sizeof(vals));
    vals.mask = 1ULL;
    if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
        log_error("touch: gpio_get: %s", strerror(errno));
        return -1;
    }
    return (int)(vals.bits & 1ULL);
}

/* -------------------------------------------------------------------------
 * Low-level XPT2046 read
 * ---------------------------------------------------------------------- */

static uint16_t xpt_read_raw(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = 3,
        .speed_hz      = TOUCH_SPI_HZ,
        .bits_per_word = 8,
        .delay_usecs   = 0,
    };

    if (ioctl(s_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0)
        return 0;

    return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
}

static uint16_t xpt_read_avg(uint8_t cmd)
{
    uint32_t sum = 0;
    for (int i = 0; i < TOUCH_SAMPLES; i++)
        sum += xpt_read_raw(cmd);
    return (uint16_t)(sum / TOUCH_SAMPLES);
}

/* -------------------------------------------------------------------------
 * Coordinate mapping
 * ---------------------------------------------------------------------- */

static uint16_t map_x(uint16_t raw)
{
    if (raw <= TOUCH_X_MIN) return 0;
    if (raw >= TOUCH_X_MAX) return TFT_WIDTH - 1;
    return (uint16_t)((uint32_t)(raw - TOUCH_X_MIN) * (TFT_WIDTH - 1)
                      / (TOUCH_X_MAX - TOUCH_X_MIN));
}

static uint16_t map_y(uint16_t raw)
{
    if (raw <= TOUCH_Y_MIN) return 0;
    if (raw >= TOUCH_Y_MAX) return TFT_HEIGHT - 1;
    return (uint16_t)((uint32_t)(raw - TOUCH_Y_MIN) * (TFT_HEIGHT - 1)
                      / (TOUCH_Y_MAX - TOUCH_Y_MIN));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

gm_status_t touch_open(const char *spi_device, int irq_gpio)
{
    s_gpio_fd = open(TOUCH_GPIOCHIP, O_RDONLY);
    if (s_gpio_fd < 0) {
        log_error("touch: open %s: %s", TOUCH_GPIOCHIP, strerror(errno));
        return GM_ERR_IO;
    }

    s_irq_fd = gpio_request_input(s_gpio_fd, (unsigned int)irq_gpio);
    if (s_irq_fd < 0) {
        log_error("touch: failed to acquire IRQ GPIO %d", irq_gpio);
        close(s_gpio_fd); s_gpio_fd = -1;
        return GM_ERR_IO;
    }

    s_spi_fd = open(spi_device, O_RDWR);
    if (s_spi_fd < 0) {
        log_error("touch: open %s: %s", spi_device, strerror(errno));
        close(s_irq_fd);  s_irq_fd  = -1;
        close(s_gpio_fd); s_gpio_fd = -1;
        return GM_ERR_IO;
    }

    uint8_t mode = SPI_MODE_0;
    ioctl(s_spi_fd, SPI_IOC_WR_MODE, &mode);
    uint8_t bits = 8;
    ioctl(s_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    uint32_t speed = TOUCH_SPI_HZ;
    ioctl(s_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    log_info("touch: XPT2046 ready on %s (IRQ=%d)", spi_device, irq_gpio);
    return GM_OK;
}

bool touch_read(gm_touch_point_t *out)
{
    if (s_spi_fd < 0 || !out) return false;

    /* Fast path: T_IRQ high = no touch */
    if (gpio_get(s_irq_fd) == 1) return false;

    uint16_t raw_x  = xpt_read_avg(XPT_READ_X);
    uint16_t raw_y  = xpt_read_avg(XPT_READ_Y);
    uint16_t raw_z1 = xpt_read_avg(XPT_READ_Z1);
    uint16_t raw_z2 = xpt_read_avg(XPT_READ_Z2);

    int32_t z = (int32_t)raw_z1 - (int32_t)raw_z2 + 4095;
    if (z < TOUCH_Z_THRESHOLD) return false;

    out->x = map_x(raw_x);
    out->y = map_y(raw_y);
    out->z = (z > 0xFFFF) ? 0xFFFF : (uint16_t)z;
    return true;
}

void touch_close(void)
{
    if (s_spi_fd  >= 0) { close(s_spi_fd);  s_spi_fd  = -1; }
    if (s_irq_fd  >= 0) { close(s_irq_fd);  s_irq_fd  = -1; }
    if (s_gpio_fd >= 0) { close(s_gpio_fd); s_gpio_fd = -1; }
    log_info("touch: closed");
}