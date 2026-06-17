/**
 * @file ui/gpio_button.c
 * @brief GPIO button polling — Linux GPIO character device (kernel 6.x).
 *
 * Active-low buttons with internal pull-ups.
 * Debounce: one accepted event per DEBOUNCE_MS window.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/gpio.h>

#include "ui/gpio_button.h"
#include "util/log.h"

/* --------------------------------------------------------------------------
 * Hardware config
 * -------------------------------------------------------------------------- */

#define GPIO_CHIP        "/dev/gpiochip0"
#define DEBOUNCE_MS      50u

static const unsigned int k_gpio_lines[BTN_COUNT] = {
    [BTN_UP]     =  5,
    [BTN_DOWN]   =  6,
    [BTN_LEFT]   = 13,
    [BTN_RIGHT]  = 19,
    [BTN_CENTER] = 26,
};

static const InputEvent k_btn_event[BTN_COUNT] = {
    [BTN_UP]     = INPUT_BTN_UP,
    [BTN_DOWN]   = INPUT_BTN_DOWN,
    [BTN_LEFT]   = INPUT_BTN_LEFT,
    [BTN_RIGHT]  = INPUT_BTN_RIGHT,
    [BTN_CENTER] = INPUT_BTN_CENTER,
};

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */

static int s_chip_fd = -1;
static int s_line_fd = -1;   /* single multi-line request fd */
static uint32_t s_last_event_ms = 0;

/* --------------------------------------------------------------------------
 * Monotonic clock helper
 * -------------------------------------------------------------------------- */

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

gm_status_t gpio_button_open(void)
{
    s_chip_fd = open(GPIO_CHIP, O_RDONLY);
    if (s_chip_fd < 0) {
        log_error("gpio_button: open %s: %s", GPIO_CHIP, strerror(errno));
        return GM_ERR_IO;
    }

    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));

    req.num_lines = BTN_COUNT;
    for (int i = 0; i < BTN_COUNT; i++)
        req.offsets[i] = k_gpio_lines[i];

    snprintf(req.consumer, sizeof(req.consumer), "geomark-buttons");
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT
                     | GPIO_V2_LINE_FLAG_BIAS_PULL_UP
                     | GPIO_V2_LINE_FLAG_ACTIVE_LOW;

    if (ioctl(s_chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        log_error("gpio_button: request lines: %s", strerror(errno));
        close(s_chip_fd);
        s_chip_fd = -1;
        return GM_ERR_IO;
    }

    s_line_fd = req.fd;
    log_info("gpio_button: 5 buttons ready on gpiochip0");
    return GM_OK;
}

InputEvent gpio_button_poll(void)
{
    if (s_line_fd < 0)
        return INPUT_NONE;

    uint32_t now = monotonic_ms();
    if ((now - s_last_event_ms) < DEBOUNCE_MS)
        return INPUT_NONE;

    struct gpio_v2_line_values vals;
    memset(&vals, 0, sizeof(vals));
    vals.mask = (1ULL << BTN_COUNT) - 1ULL;  /* all 5 lines */

    if (ioctl(s_line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
        log_error("gpio_button: get values: %s", strerror(errno));
        return INPUT_NONE;
    }

    /* Active-low with GPIO_V2_LINE_FLAG_ACTIVE_LOW set:
     * bit=1 means button pressed (kernel inverts for us). */
    for (int i = 0; i < BTN_COUNT; i++) {
        if (vals.bits & (1ULL << i)) {
            s_last_event_ms = now;
            return k_btn_event[i];
        }
    }

    return INPUT_NONE;
}

void gpio_button_close(void)
{
    if (s_line_fd  >= 0) { close(s_line_fd);  s_line_fd  = -1; }
    if (s_chip_fd  >= 0) { close(s_chip_fd);  s_chip_fd  = -1; }
    log_info("gpio_button: closed");
}