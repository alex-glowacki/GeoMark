/**
 * @file radio.c
 * @brief SiK 915 MHz telemetry radio interface (thin wrapper over serial).
 */

#include "stream/radio.h"

#include <string.h>

SerialResult radio_open(Radio *r, const char *device, int baud)
{
    if (!r || !device) {
        return SERIAL_ERR_ARG;
    }
    memset(r, 0, sizeof(*r));
    /* 100 ms read timeout — enough headroom for the collector thread */
    return serial_open(&r->serial, device, baud, 100);
}

int radio_read(Radio *r, uint8_t *buf, size_t len)
{
    if (!r || !buf) {
        return SERIAL_ERR_ARG;
    }
    return serial_read(&r->serial, buf, len);
}

SerialResult radio_write(Radio *r, const uint8_t *buf, size_t len)
{
    if (!r || !buf) {
        return SERIAL_ERR_ARG;
    }
    return serial_write(&r->serial, buf, len);
}

void radio_close(Radio *r)
{
    if (!r) {
        return;
    }
    serial_close(&r->serial);
}