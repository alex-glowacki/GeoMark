/**
 * @file touch.c
 * @brief XPT2046 resistive touch controller via Linux spidev.
 *
 * XPT2046 command byte format (datasheet §5.1):
 *   Bit 7   : Start bit (always 1)
 *   Bits 6-4: Channel select (A2:A0)
 *             X position: 101 (0x50 | start) = 0xD0
 *             Y position: 001 (0x10 | start) = 0x90
 *             Z1 pressure: 011                = 0xB0
 *             Z2 pressure: 100                = 0xC0
 *   Bit 3   : MODE — 0 = 12-bit, 1 = 8-bit  (use 0)
 *   Bit 2   : SER/DFR — 0 = differential (use 0 for ratiometric accuracy)
 *   Bits 1-0: Power-down mode — 00 = power down between conversions
 *
 * Full command bytes (12-bit differential):
 *   READ_X  = 0xD0
 *   READ_Y  = 0x90
 *   READ_Z1 = 0xB0
 *   READ_Z2 = 0xC0
 *
 * Each SPI transaction is 3 bytes: [cmd, 0x00, 0x00].
 * Response is in bytes [1] and [2]: result = ((b1 << 8) | b2) >> 3.
 *
 * Pressure formula: Z = Z1 - Z2 + 4095 (simplified; positive = touch).
 *
 * SPI: mode 0, max 2.5 MHz (XPT2046 datasheet §6).
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "ui/tft/touch.h"
#include "ui/tft/display.h"  /* for TFT_WIDTH / TFT_HEIGHT */
#include "util/log.h"

/* -------------------------------------------------------------------------
 * XPT2046 command bytes
 * ---------------------------------------------------------------------- */

#define XPT_READ_X  0xD0
#define XPT_READ_Y  0x90
#define XPT_READ_Z1 0xB0
#define XPT_READ_Z2 0xC0

/* Minimum Z pressure threshold to register a touch */
#define TOUCH_Z_THRESHOLD 100

/* Number of ADC samples to average per read */
#define TOUCH_SAMPLES 8

/* SPI clock: 2 MHz — well within XPT2046's 2.5 MHz limit */
#define TOUCH_SPI_HZ 2000000u

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static int s_spi_fd  = -1;
static int s_irq_gpio = -1;

/* -------------------------------------------------------------------------
 * sysfs GPIO input helper (IRQ pin — input only)
 * ---------------------------------------------------------------------- */

static int gpio_export_input(int gpio)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    if (access(path, F_OK) != 0) {
        int fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd < 0) return -1;
        char buf[8];
        int n = snprintf(buf, sizeof(buf), "%d", gpio);
        if (write(fd, buf, (size_t)n) < 0) { close(fd); return -1; }
        close(fd);
        usleep(10000);
    }
    /* Set direction to "in" */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    if (write(fd, "in", 2) < 0) { close(fd); return -1; }
    close(fd);
    return 0;
}

static int gpio_read(int gpio)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char val = '0';
    ssize_t r = read(fd, &val, 1);
    (void)r; /* best-effort: failure falls through to returning 0 */
    close(fd);
    return (val == '1') ? 1 : 0;
}

static void gpio_unexport(int gpio)
{
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) return;
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", gpio);
    ssize_t r = write(fd, buf, (size_t)n);
    (void)r; /* best-effort: failure here is non-fatal */
    close(fd);
}

/* -------------------------------------------------------------------------
 * Low-level XPT2046 read
 * ---------------------------------------------------------------------- */

/*
 * Send a single 3-byte SPI transaction and return the 12-bit ADC result.
 * XPT2046 returns data MSB-first, result in bits [14:3] of the 24-bit
 * response → shift right by 3.
 */
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

/*
 * Averaged read of one channel.
 */
static uint16_t xpt_read_avg(uint8_t cmd)
{
    uint32_t sum = 0;
    for (int i = 0; i < TOUCH_SAMPLES; i++)
        sum += xpt_read_raw(cmd);
    return (uint16_t)(sum / TOUCH_SAMPLES);
}

/* -------------------------------------------------------------------------
 * Coordinate mapping: ADC value → screen pixel
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
    s_irq_gpio = irq_gpio;

    if (gpio_export_input(irq_gpio) < 0) {
        log_error("touch: failed to configure IRQ GPIO %d", irq_gpio);
        return GM_ERR_IO;
    }

    s_spi_fd = open(spi_device, O_RDWR);
    if (s_spi_fd < 0) {
        log_error("touch: open %s: %s", spi_device, strerror(errno));
        return GM_ERR_IO;
    }

    uint8_t mode = SPI_MODE_0;
    if (ioctl(s_spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        log_error("touch: SPI_IOC_WR_MODE: %s", strerror(errno));
        close(s_spi_fd); s_spi_fd = -1;
        return GM_ERR_IO;
    }

    uint8_t bits = 8;
    ioctl(s_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);

    uint32_t speed = TOUCH_SPI_HZ;
    ioctl(s_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    log_info("touch: XPT2046 ready on %s (IRQ=%d)", spi_device, irq_gpio);
    return GM_OK;
}

bool touch_read(gm_touch_point_t *out)
{
    if (s_spi_fd < 0 || !out)
        return false;

    /* Fast path: T_IRQ high means no touch */
    if (gpio_read(s_irq_gpio) == 1)
        return false;

    uint16_t raw_x  = xpt_read_avg(XPT_READ_X);
    uint16_t raw_y  = xpt_read_avg(XPT_READ_Y);
    uint16_t raw_z1 = xpt_read_avg(XPT_READ_Z1);
    uint16_t raw_z2 = xpt_read_avg(XPT_READ_Z2);

    /* Pressure: positive and above threshold = real touch */
    int32_t z = (int32_t)raw_z1 - (int32_t)raw_z2 + 4095;
    if (z < TOUCH_Z_THRESHOLD)
        return false;

    out->x = map_x(raw_x);
    out->y = map_y(raw_y);
    out->z = (z > 0xFFFF) ? 0xFFFF : (uint16_t)z;

    return true;
}

void touch_close(void)
{
    if (s_spi_fd >= 0) {
        close(s_spi_fd);
        s_spi_fd = -1;
    }
    if (s_irq_gpio >= 0) {
        gpio_unexport(s_irq_gpio);
        s_irq_gpio = -1;
    }
    log_info("touch: closed");
}