/**
 * @file config.c
 * @brief INI-style configuration parser implementation.
 */

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "geomark.h"
#include "util/config.h"
#include "util/log.h"

void config_defaults(gm_config_t *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->serial_device, "/dev/ttyAMA0", sizeof(out->serial_device) - 1);
    out->serial_baud = 115200;
    strncpy(out->radio_device, "dev/ttyUSB0", sizeof(out->radio_device) - 1);
    out->radio_baud = 57600;
    strncpy(out->log_file, "/var/log/geomark.log", sizeof(out->log_file) - 1);
    strncpy(out->data_dir, "/var/lib/geomark", sizeof(out->data_dir) - 1);
}

gm_status_t config_load(const char *path, gm_config_t *out) {
    config_defaults(out);

    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("config_load: cannot open '%s': %s — using defaults",
                path, strerror(errno));
        return GM_OK;
    }

    char line[GM_CONFIG_MAX_LINE];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        /* Strip trailing newline / carriage-return */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip blank lines and comments */
        if (len == 0 || line[0] == '#' || line[0] == ';')
            continue;

        /* Split on first '=' */
        char *eq = strchr(line, '=');
        if (!eq) {
            log_warn("config_load: %s:%d: malformed line (no '=') — skipped",
                    path, lineno);
            continue;
        }

        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* Match known keys */
        if (strcmp(key, "serial_device") == 0) {
            strncpy(out->serial_device, val, sizeof(out->serial_device) - 1);
        } else if (strcmp(key, "serial_baud") == 0) {
            out->serial_baud = atoi(val);
        } else if (strcmp(key, "radio_device") == 0) {
            strncpy(out->radio_device, val, sizeof(out->radio_device) - 1);
        } else if (strcmp(key, "radio_baud") == 0) {
            out->radio_baud = atoi(val);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(out->log_file, val, sizeof(out->log_file) - 1);
        } else if (strcmp(key, "data_dir") == 0) {
            strncpy(out->data_dir, val, sizeof(out->data_dir) - 1);
        } else {
            log_warn("config_load: %s:%d: unknown key '%s' — ignored",
                    path, lineno, key);
        }
    }

    fclose(f);
    log_info("config_load: loaded '%s'", path);
    return GM_OK;
}