/**
 * @file config.c
 * @brief INI-style configuration parser implementation.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util/config.h"
#include "util/log.h"

void config_defaults(gm_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->serial_device, "/dev/ttyAMA0", sizeof(out->serial_device) - 1);
    out->serial_baud = 115200;
    strncpy(out->radio_device, "/dev/ttyUSB0", sizeof(out->radio_device) - 1);
    out->radio_baud  = 57600;
    strncpy(out->log_file, "/var/log/geomark.log", sizeof(out->log_file) - 1);
    strncpy(out->data_dir, "/var/lib/geomark",     sizeof(out->data_dir) - 1);
}

gm_status_t config_load(const char *path, gm_config_t *out)
{
    /* TODO: Phase 2 implementation */
    (void)path;
    log_warn("config_load: not yet implemented — using defaults");
    config_defaults(out);
    return GM_OK;
}