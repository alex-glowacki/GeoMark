/**
 * @file um980.h
 * @brief UM980 GNSS module command interface and initialization.
 */

#ifndef GEOMARK_UM980_H
#define GEOMARK_UM980_H

#include "geomark.h"
#include "stream/serial.h"

typedef struct {
    gm_serial_t serial;
    char device[64];
    int baud;
} gm_um980_t;

gm_status_t um980_open(gm_um980_t *u, const char *device, int baud);
gm_status_t um980_send_command(gm_um980_t *u, const char *cmd);
gm_status_t um980_init_base(gm_um980_t *u);
gm_status_t um980_init_rover(gm_um980_t *u);
gm_status_t um980_read_raw(gm_um980_t *u, uint8_t *buf, size_t len, size_t *out_len);
void um980_close(gm_um980_t *u);

#endif /* GEOMARK_UM980_H */