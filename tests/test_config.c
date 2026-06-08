/**
 * @file test_config.c
 * @brief Unit tests for INI configuration parser.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "util/config.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg)                          \
    do {                                           \
        if (cond) {                                \
            printf("  PASS: %s\n", (msg));         \
            passed++;                              \
        } else {                                   \
            printf("  FAIL: %s\n", (msg));         \
            failed++;                              \
        }                                          \
    } while (0)

int main(void)
{
    printf("=== test_config ===\n");

    gm_config_t cfg;
    config_defaults(&cfg);

    ASSERT(cfg.serial_baud == 115200,     "default serial baud is 115200");
    ASSERT(cfg.radio_baud  == 57600,      "default radio baud is 57600");
    ASSERT(strlen(cfg.serial_device) > 0, "default serial device is set");
    ASSERT(strlen(cfg.log_file) > 0,      "default log file path is set");
    ASSERT(strlen(cfg.data_dir) > 0,      "default data directory is set");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}