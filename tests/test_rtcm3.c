/**
 * @file test_rtcm3.c
 * @brief Unit tests for RTCM3 binary frame parser.
 */

#include <stdio.h>
#include <stdbool.h>

#include "gnss/rtcm3.h"

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
    printf("=== test_rtcm3 ===\n");

    /* TODO: Phase 1 — real assertions added once rtcm3.c is implemented */
    ASSERT(true, "placeholder — rtcm3 tests not yet implemented");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}