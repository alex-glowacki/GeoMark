/**
 * @file um980.c
 * @brief UM980 GNSS module command interface implementation.
 */

#include <string.h>

#include "gnss/um980.h"
#include "util/log.h"

gm_status_t um980_open(gm_um980_t *u, const char *device, int baud)
{
    /* TODO: Phase 1 implementation */
    (void)u;
    (void)device;
    (void)baud;
    log_warn("um980_open: not yet implemented");
    return GM_ERR_GENERIC;
}

gm_status_t um980_send_command(gm_um980_t *u, const char *cmd)
{
    /* TODO: Phase 1 implementation */
    (void)u;
    (void)cmd;
    return GM_ERR_GENERIC;
}

gm_status_t um980_init_base(gm_um980_t *u)
{
    /* TODO: Phase 1 implementation */
    (void)u;
    return GM_ERR_GENERIC;
}

gm_status_t um980_init_rover(gm_um980_t *u)
{
    /* TODO: Phase 1 implementation */
    (void)u;
    return GM_ERR_GENERIC;
}

gm_status_t um980_read_raw(gm_um980_t *u, uint8_t *buf, size_t len, size_t *out_len)
{
    /* TODO: Phase 1 implementation */
    (void)u;
    (void)buf;
    (void)len;
    (void)out_len;
    return GM_ERR_GENERIC;
}

void um980_close(gm_um980_t *u)
{
    serial_close(&u->serial);
}