/**
 * @file serial.c
 * @brief POSIX serial port implementation (termios).
 */

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#include "stream/serial.h"
#include "util/log.h"

gm_status_t serial_open(gm_serial_t *s, const char *device, int baud)
{
    /* TODO: Phase 1 implementation */
    (void)s;
    (void)device;
    (void)baud;
    log_warn("serial_open: not yet implemented");
    return GM_ERR_GENERIC;
}

gm_status_t serial_read(gm_serial_t *s, uint8_t *buf, size_t len, size_t *out_len)
{
    (void)s;
    (void)buf;
    (void)len;
    (void)out_len;
    return GM_ERR_GENERIC;
}

gm_status_t serial_write(gm_serial_t *s, const uint8_t *buf, size_t len)
{
    (void)s;
    (void)buf;
    (void)len;
    return GM_ERR_GENERIC;
}

void serial_close(gm_serial_t *s)
{
    if (s && s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
}