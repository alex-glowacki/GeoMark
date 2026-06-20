/**
 * @file ui/gpio_button.c
 * @brief GPIO button polling — Linux GPIO character device (kernel 6.x).
 *
 * Active-low buttons with internal pull-ups.
 *
 * Edge detection, not level polling: gpio_button_poll() only reports an
 * event on the low->high (released->pressed) transition of each line,
 * tracked per-line in s_prev_pressed. The previous implementation
 * returned whatever the pins currently read on every call -- while a
 * button was physically held, that meant the same event fired
 * repeatedly on every poll past the (too-short) 50ms debounce window,
 * for as long as the button stayed down. Observed on real hardware as
 * "tap/press advances to the next screen, but reverts the instant the
 * button or touch is released" -- the destination screen's on_event
 * doesn't consume the repeated activation, and depending on which
 * line's repeated/ghosted read won the first-match scan below, a
 * mechanical release bounce could surface as a different button
 * entirely (e.g. an apparent BACK), popping the stack straight back.
 *
 * Each line now also gets its own debounce hold-off after a registered
 * press, rather than one global debounce timestamp shared across all 5
 * lines -- the previous single s_last_event_ms meant pressing button A
 * could suppress a genuine, separate press of button B within the same
 * 50ms window.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
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

static int      s_chip_fd  = -1;
static int      s_line_fd  = -1;   /* single multi-line request fd */

/* Per-line edge-detection state. */
static bool     s_prev_pressed[BTN_COUNT];   /* line state as of the last poll */
static uint32_t s_last_press_ms[BTN_COUNT];  /* last time THIS line registered a press */

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

    memset(s_prev_pressed, 0, sizeof(s_prev_pressed));
    memset(s_last_press_ms, 0, sizeof(s_last_press_ms));

    log_info("gpio_button: 5 buttons ready on gpiochip0");
    return GM_OK;
}

InputEvent gpio_button_poll(void)
{
    if (s_line_fd < 0)
        return INPUT_NONE;

    struct gpio_v2_line_values vals;
    memset(&vals, 0, sizeof(vals));
    vals.mask = (1ULL << BTN_COUNT) - 1ULL;  /* all 5 lines */

    if (ioctl(s_line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
        log_error("gpio_button: get values: %s", strerror(errno));
        return INPUT_NONE;
    }

    uint32_t now = monotonic_ms();

    /* Active-low with GPIO_V2_LINE_FLAG_ACTIVE_LOW set:
     * bit=1 means button pressed (kernel inverts for us). */
    for (int i = 0; i < BTN_COUNT; i++) {
        bool pressed_now = (vals.bits & (1ULL << i)) != 0;
        bool was_pressed = s_prev_pressed[i];
        s_prev_pressed[i] = pressed_now;

        /* Only the released->pressed transition is a candidate event --
         * a button held down across many poll calls reports pressed_now
         * == true == was_pressed on every call after the first, and is
         * correctly ignored here. */
        if (!pressed_now || was_pressed)
            continue;

        /* Per-line debounce: ignore a new press on this specific line
         * if one was already registered on it too recently (mechanical
         * bounce on contact). Does not affect any other line. */
        if ((now - s_last_press_ms[i]) < DEBOUNCE_MS)
            continue;

        s_last_press_ms[i] = now;
        return k_btn_event[i];
    }

    return INPUT_NONE;
}

void gpio_button_close(void)
{
    if (s_line_fd  >= 0) { close(s_line_fd);  s_line_fd  = -1; }
    if (s_chip_fd  >= 0) { close(s_chip_fd);  s_chip_fd  = -1; }
    log_info("gpio_button: closed");
}