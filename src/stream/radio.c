/**
 * @file radio.c
 * @brief SiK radio abstraction — thin wrapper over serial.
 */

#include "stream/radio.h"

gm_status_t radio_open(gm_radio_t *r, const char *device, int baud)
{
    return serial_open(&r->serial, device, baud);
}

gm_status_t radio_read(gm_radio_t *r, uint8_t *buf, size_t len, size_t *out_len)
{
    return serial_read(&r->serial, buf, len, out_len);
}

gm_status_t radio_write(gm_radio_t *r, const uint8_t *buf, size_t len)
{
    return serial_write(&r->serial, buf, len);
}

void radio_close(gm_radio_t *r)
{
    serial_close(&r->serial);
}