/**
 * @file touch_input.c
 * @brief evdev multitouch input for the Hosyond 7" DSI capacitive panel.
 *
 * Device discovery: scans /dev/input/event0..31, opens each, and asks via
 * EVIOCGBIT(EV_ABS, ...) whether it reports ABS_MT_POSITION_X. The first
 * match wins. This avoids hardcoding an eventN path, since the actual
 * node number depends on driver probe order and varies across
 * ft5x06/edt_ft5x06/goodix-style drivers -- confirmed by checking how
 * other DSI capacitive panels enumerate on Raspberry Pi OS before writing
 * this scan logic.
 *
 * Tap resolution: a tap is reported on the BTN_TOUCH release transition
 * (1 -> 0), using the most recent ABS_MT_POSITION_X/Y seen for the
 * contact. This collapses an entire touch-down/move/touch-up sequence
 * into exactly one UI_EVENT_TAP, matching what ui_grid_handle_event()
 * expects (one discrete activation per physical tap, same as a single
 * button press) rather than firing on every intermediate position update.
 */

#define _GNU_SOURCE

#include "ui/core/touch_input.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "ui/tft/display.h"
#include "util/log.h"

#define TOUCH_MAX_SCAN_NODES 32
#define TOUCH_DEV_PATH_FMT   "/dev/input/event%d"

/* Minimum time between two resolved taps. Real capacitive contact can
 * produce a brief BTN_TOUCH bounce (down-up-down-up within a few
 * milliseconds) right at the moment of contact or release, which without
 * a debounce window resolves as two separate taps from a single physical
 * touch -- observed on real hardware as "tap advances the screen, then
 * immediately bounces back" (the second spurious tap landing wherever
 * the finger was actually lifted, which may hit a different control on
 * the newly-shown screen). 150ms is comfortably longer than any
 * mechanical/capacitive bounce but short enough not to feel laggy for a
 * deliberate second tap. */
#define TOUCH_DEBOUNCE_MS 150u

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static int      s_fd            = -1;
static int32_t  s_last_x        = -1;
static int32_t  s_last_y        = -1;
static bool     s_touch_down    = false;
static uint32_t s_last_tap_ms   = 0;     /* monotonic time of last resolved tap */
static bool     s_have_last_tap = false; /* false until the first tap ever resolves */

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* -------------------------------------------------------------------------
 * Device discovery
 * ---------------------------------------------------------------------- */

static bool device_has_mt_position(int fd)
{
    unsigned long abs_bits[(ABS_MAX / (8 * sizeof(unsigned long))) + 1];
    memset(abs_bits, 0, sizeof(abs_bits));

    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0)
        return false;

    size_t idx = ABS_MT_POSITION_X / (8 * sizeof(unsigned long));
    size_t bit = ABS_MT_POSITION_X % (8 * sizeof(unsigned long));
    return (abs_bits[idx] & (1UL << bit)) != 0;
}

static int find_touch_device(void)
{
    char path[64];

    for (int i = 0; i < TOUCH_MAX_SCAN_NODES; i++) {
        snprintf(path, sizeof(path), TOUCH_DEV_PATH_FMT, i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        if (device_has_mt_position(fd)) {
            char name[128] = "(unknown)";
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            log_info("touch_input: using %s (\"%s\")", path, name);
            return fd;
        }

        close(fd);
    }

    return -1;
}

/* -------------------------------------------------------------------------
 * Coordinate clamp -- evdev devices report their own native resolution
 * (queried via ABS_MT_POSITION_X/Y min/max), which the Hosyond panel's
 * driver-free capacitive controller maps 1:1 to panel pixels (800x480),
 * so no separate calibration scaling is needed here, unlike the old
 * XPT2046 resistive controller's raw-ADC-to-pixel mapping.
 * ---------------------------------------------------------------------- */

static uint16_t clamp_coord(int32_t v, uint16_t max_exclusive)
{
    if (v < 0) return 0;
    if (v >= (int32_t)max_exclusive) return (uint16_t)(max_exclusive - 1);
    return (uint16_t)v;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

gm_status_t touch_input_open(void)
{
    s_fd = find_touch_device();
    if (s_fd < 0) {
        log_error("touch_input: no capacitive touch device found");
        return GM_ERR_IO;
    }

    s_last_x        = -1;
    s_last_y        = -1;
    s_touch_down    = false;
    s_last_tap_ms   = 0;
    s_have_last_tap = false;
    return GM_OK;
}

bool touch_input_poll(UiEvent *out)
{
    if (s_fd < 0 || !out) return false;

    struct input_event ev;

    /* Drain pending events one at a time. Stop as soon as a tap resolves
     * rather than continuing to drain the rest of the buffer -- a second
     * BTN_TOUCH down/up pair later in the same read burst (a contact
     * bounce from the same physical touch) must not be allowed to
     * silently replace the tap we already resolved this call. Any events
     * left unread stay queued by the kernel and get picked up on the
     * next poll, so nothing is lost -- they just won't resolve a second
     * tap inside the debounce window below. */
    while (read(s_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                s_last_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                s_last_y = ev.value;
            break;

        case EV_KEY:
            if (ev.code == BTN_TOUCH) {
                if (ev.value == 1) {
                    s_touch_down = true;
                } else if (ev.value == 0 && s_touch_down) {
                    /* Release: resolve one tap at the last known position,
                     * unless it arrives within the debounce window after
                     * the previous resolved tap (contact bounce, not a
                     * deliberate second tap). */
                    s_touch_down = false;

                    uint32_t now = monotonic_ms();
                    bool debounced = s_have_last_tap &&
                                      (now - s_last_tap_ms) < TOUCH_DEBOUNCE_MS;

                    if (!debounced && s_last_x >= 0 && s_last_y >= 0) {
                        out->type = UI_EVENT_TAP;
                        out->x = clamp_coord(s_last_x, TFT_WIDTH);
                        out->y = clamp_coord(s_last_y, TFT_HEIGHT);

                        s_last_tap_ms   = now;
                        s_have_last_tap = true;

                        return true;
                    }
                }
            }
            break;

        case EV_SYN:
        default:
            break;
        }
    }

    return false;
}

void touch_input_close(void)
{
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
    log_info("touch_input: closed");
}