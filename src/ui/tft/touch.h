/**
 * @file touch.h
 * @brief XPT2046 resistive touch controller via SPI.
 */

#ifndef GEOMARK_TOUCH_H
#define GEOMARK_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "geomark.h"

typedef struct {
    uint16_t x;
    uint16_t y;
} gm_touch_point_t;

gm_status_t touch_open(const char *spi_device, int irq_gpio);
bool touch_read(gm_touch_point_t *out);
void touch_close(void);

#endif /* GEOMARK_TOUCH_H */