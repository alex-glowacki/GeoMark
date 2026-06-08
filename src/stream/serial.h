/**
 * @file serial.h
 * @brief POSIX serial port open/read/write (termios).
 */

#ifndef GEOMARK_SERIAL_H
#define GEOMARK_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#include "geomark.h"

typedef struct {
    int fd;
    char device[64];
    int baud;
} gm_serial_t;

gm_status_t serial_open(gm_serial_t *s, const char *device, int baud);
gm_status_t serial_read(gm_serial_t *s, uint8_t *buf, size_t len, size_t *out_len);
gm_status_t serial_write(gm_serial_t *s, const uint8_t *buf, size_t len);
void serial_close(gm_serial_t *s);

#endif /* GEOMARK_SERIAL_H */