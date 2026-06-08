/**
 * @file display.c
 * @brief ST7796S SPI display driver — Phase 4.
 *
 * HARDWARE PREREQUISITE: 6-channel 3.3V<->5V level shifter required on all
 * SPI lines before this module is wired or implemented. Do not connect the
 * display directly to Pi GPIO.
 */

#include "ui/tft/display.h"
#include "util/log.h"

gm_status_t display_open(const char *spi_device, int dc_gpio, int rst_gpio)
{
    /* TODO: Phase 4 implementation */
    (void)spi_device;
    (void)dc_gpio;
    (void)rst_gpio;
    log_warn("display_open: not yet implemented");
    return GM_ERR_GENERIC;
}

gm_status_t display_init(void)
{
    /* TODO: Phase 4 implementation */
    return GM_ERR_GENERIC;
}

void display_fill(uint16_t color)
{
    /* TODO: Phase 4 implementation */
    (void)color;
}

void display_close(void)
{
    /* TODO: Phase 4 implementation */
}