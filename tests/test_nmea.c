/**
 * @file test_nmea.c
 * @brief Unit tests for NMEA 0183 sentence parser.
 */

#include <stdio.h>
#include <stdbool.h>

#include "gnss/nmea.h"

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
    printf("=== test_nmea ===\n");

    /* TODO: Phase 1 — real assertions added once nmea.c is implemented */
    ASSERT(true, "placeholder — nmea tests not yet implemented");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}