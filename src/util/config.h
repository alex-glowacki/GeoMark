/**
 * @file config.h
 * @brief INI-style configuration file parser.
 */

#ifndef GEOMARK_CONFIG_H
#define GEOMARK_CONFIG_H

#include "geomark.h"

typedef struct {
    char serial_device[64];
    int serial_baud;
    char radio_device[64];
    int radio_baud;
    char log_file[256];
    char data_dir[256];
} gm_config_t;

gm_status_t config_load(const char *path, gm_config_t *out);
void config_defaults(gm_config_t *out);

#endif /* GEOMARK_CONFIG_H */