/**
 * @file test_config.c
 * @brief Unit tests for INI configuration parser.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "geomark.h"
#include "util/config.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg)                       \
    do {                                        \
        if (cond) {                             \
            printf("  PASS: %s\n", (msg));      \
            passed++;                           \
        } else {                                \
            printf("  FAIL: %s\n", (msg));      \
            failed++;                           \
        }                                       \
    } while (0)

/* Write a temporary INI file and return its path (caller must free). */
static char *write_tmp_ini(const char *contents) {
    char *path = strdup("/tmp/geomark_test_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) {
        free(path);
        return NULL;
    }
    FILE *f = fdopen(fd, "w");
    fputs(contents, f);
    fclose(f);
    return path;
}

int main(void) {
    printf("=== test_config ===\n");

     /* ------------------------------------------------------------------ */
     printf("-- defaults\n");
     {
        gm_config_t cfg;
        config_defaults(&cfg);

        ASSERT(cfg.serial_baud == 115200, "default serial baud is 115200");
        ASSERT(cfg.radio_baud == 57600, "default radio baud is 57600");
        ASSERT(strlen(cfg.serial_device) > 0, "default serial device is set");
        ASSERT(strlen(cfg.log_file) > 0, "default log path is set");
        ASSERT(strlen(cfg.data_dir) > 0, "default data directory is set");
     }

     /* ------------------------------------------------------------------ */
     printf("-- load full valid config\n");
     {
        char *path = write_tmp_ini(
            "serial_device=/dev/ttyS0\n"
            "serial_baud=9600\n"
            "radio_device=/dev/ttyUSB1\n"
            "radio_baud=38400\n"
            "log_file=/tmp/geomark_test.log\n"
            "data_dir=/tmp/geomark_data\n"
        );

        gm_config_t cfg;
        gm_status_t rc = config_load(path, &cfg);
        free(path);

        ASSERT(rc == GM_OK, "config_load return GM_OK");
        ASSERT(strcmp(cfg.serial_device, "/dev/ttyS0") == 0, "serial_device parsed");
        ASSERT(cfg.serial_baud == 9600, "serial_baud parsed");
        ASSERT(strcmp(cfg.radio_device, "/dev/ttyUSB1") == 0, "radio_device parsed");
        ASSERT(cfg.radio_baud == 38400, "radio_baud parsed");
        ASSERT(strcmp(cfg.log_file, "/tmp/geomark_test.log") == 0, "log_file parsed");
        ASSERT(strcmp(cfg.data_dir, "/tmp/geomark_data") == 0, "data_dir parsed");
     }

     /* ------------------------------------------------------------------ */
     printf("-- comments and blank lines are skipped\n");
     {
        char *path = write_tmp_ini(
            "# This is a comment\n"
            "\n"
            "; Another comment style\n"
            "serial_baud=4800\n"
        );

        gm_config_t cfg;
        config_load(path, &cfg);
        free(path);

        ASSERT(cfg.serial_baud == 4800, "baud parsed after comments and blanks");
     }

     /* ------------------------------------------------------------------ */
     printf("-- missing keys retain defaults\n");
     {
        char *path = write_tmp_ini(
            "serial_baud=19200\n"
            /* radio_baud omitted — should stay at default 57600 */
        );

        gm_config_t cfg;
        config_load(path, &cfg);
        free(path);

        ASSERT(cfg.serial_baud == 19200, "specified key overrides default");
        ASSERT(cfg.radio_baud == 57600, "unspecified key retains default");
     }

     /* ------------------------------------------------------------------ */
     printf("-- missing file falls back to defaults\n");
     {
        gm_config_t cfg;
        gm_status_t rc = config_load("/tmp/geomark_nonexistent_file.conf", &cfg);

        ASSERT(rc == GM_OK, "missing file return GM_OK");
        ASSERT(cfg.serial_baud == 115200, "missing file: default serial baud");
        ASSERT(cfg.radio_baud == 57600, "missing file: default radio baud");
     }

     /* ------------------------------------------------------------------ */
     printf("-- unknown key is ignored (no crash)\n");
     {
        char *path = write_tmp_ini(
            "unknown_key=some_value\n"
            "serial_baud=2400\n"
        );

        gm_config_t cfg;
        gm_status_t rc = config_load(path, &cfg);
        free(path);

        ASSERT(rc == GM_OK, "unknown key does not cause error");
        ASSERT(cfg.serial_baud == 2400, "known key after unknown key parsed");
     }

     /* ------------------------------------------------------------------ */
     printf("Results: %d passed, %d failed\n", passed, failed);
     return (failed == 0) ? 0 : 1;
}