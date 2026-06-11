/**
 * @file um980.c
 * @brief UM980 GNSS module implementation.
 *
 * Stub — functions will be implemented in Phase 1 step 3.
 * File compiles cleanly so the build stays green.
 */

#define _GNU_SOURCE

#include "um980.h"

/* UM980 default baud rate per datasheet §3.2 */
#define UM980_BAUD        115200
#define UM980_TIMEOUT_MS  2000

SerialResult um980_open(Um980 *u, const char *device)
{
    if (!u) {
        return SERIAL_ERR_ARG;
    }
    return serial_open(&u->serial, device, UM980_BAUD, UM980_TIMEOUT_MS);
}

SerialResult um980_send_command(Um980 *u, const char *cmd)
{
    /* TODO: Phase 1 step 3 */
    (void)u;
    (void)cmd;
    return SERIAL_OK;
}

SerialResult um980_init_base(Um980 *u)
{
    /* TODO: Phase 1 step 3 */
    (void)u;
    return SERIAL_OK;
}

SerialResult um980_init_rover(Um980 *u)
{
    /* TODO: Phase 1 step 3 */
    (void)u;
    return SERIAL_OK;
}

void um980_close(Um980 *u)
{
    if (!u) {
        return;
    }
    serial_close(&u->serial);
}