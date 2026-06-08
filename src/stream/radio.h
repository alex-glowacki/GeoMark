/**
 * @file radio.h
 * @brief SiK radio abstraction — wraps serial for RTCM3 correction link.
 */

#ifndef GEOMARK_RADIO_H
#define GEOMARK_RADIO_H

#include "geomark.h"
#include "serial.h"
#include "stream/serial.h"

typedef struct {
    gm_serial_t serial;
} gm_radio_t;

gm_status_t radio_open(gm_radio_t *r, const char *device, int baud);
gm_status_t radio_read(gm_radio_t *r, uint8_t *buf, size_t len, size_t *out_len);
gm_status_t radio_write(gm_radio_t *r, const uint8_t *buf, size_t len);
void radio_close(gm_radio_t *r);

#endif /* GEOMARK_RADIO_H */