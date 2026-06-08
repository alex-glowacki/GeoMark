/**
 * @file display.h
 * @brief ST7796S SPI TFT display driver via Linux spidev.
 */

#ifndef GEOMARK_DISPLAY_H
#define GEOMARK_DISPLAY_H

#include <stdint.h>

#include "geomark.h"

#define TFT_WIDTH 480
#define TFT_HEIGHT 320

gm_status_t display_open(const char *spi_device, int dc_gpio, int rst_gpio);
gm_status_t display_init(void);
void display_fill(uint16_t color);
void display_close(void);

#endif /* GEOMARK_DISPLAY_H */