/**
 * @file touch.c
 * @brief XPT2046 touch controller — Phase 4.
 *
 * NOTE: Confirm touch controller chip markings on PCB back before
 * implementing. Expected XPT2046 but must be verified.
 */

#include "ui/tft/touch.h"
#include "util/log.h"

gm_status_t touch_open(const char *spi_device, int irq_gpio)
{
    /* TODO: Phase 4 implementation */
    (void)spi_device;
    (void)irq_gpio;
    log_warn("touch_open: not yet implemented");
    return GM_ERR_GENERIC;
}

bool touch_read(gm_touch_point_t *out)
{
    /* TODO: Phase 4 implementation */
    (void)out;
    return false;
}

void touch_close(void)
{
    /* TODO: Phase 4 implementation */
}