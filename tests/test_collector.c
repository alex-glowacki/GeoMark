#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/collector/collector.h"

/* =========================================================================
 * Minimal test harness
 * ========================================================================= */
static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        g_tests_run++;                                                         \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg));  \
            g_tests_failed++;                                                  \
        }                                                                      \
    } while (0)

/* =========================================================================
 * Inject bytes directly into the ring buffer (bypasses serial port).
 * Accesses Collector fields directly via the header — no internal symbols
 * needed.
 * ========================================================================= */
static void test_inject(Collector *c, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        size_t next = (c->head + 1) & (COLLECTOR_RING_SIZE - 1);
        if (next == c->tail)
            break; /* ring full — stop */
        c->ring[c->head] = data[i];
        c->head = next;
    }
}

/* =========================================================================
 * Callback infrastructure
 * ========================================================================= */
typedef struct {
    int            count;
    CollectorFrame last;
} CallbackCtx;

static void record_callback(const CollectorFrame *frame, void *user)
{
    CallbackCtx *ctx = (CallbackCtx *)user;
    ctx->count++;
    ctx->last = *frame;
}

/* =========================================================================
 * Helpers to set up a bare Collector (no serial port, no thread).
 * ========================================================================= */
static void init_test_collector(Collector *c, CallbackCtx *ctx)
{
    memset(c,   0, sizeof(*c));
    memset(ctx, 0, sizeof(*ctx));
    c->callback = record_callback;
    c->user     = ctx;
}

/* =========================================================================
 * Test: ring buffer byte arithmetic
 * ========================================================================= */
static void test_ring_arithmetic(void)
{
    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    ASSERT(c.head == c.tail, "Ring: starts empty (head == tail)");

    /* Push one byte manually and verify the used count. */
    c.ring[c.head] = 0x42;
    c.head = (c.head + 1) & (COLLECTOR_RING_SIZE - 1);

    size_t used = (c.head - c.tail) & (COLLECTOR_RING_SIZE - 1);
    ASSERT(used == 1, "Ring: used == 1 after one push");
}

/* =========================================================================
 * Test: valid GGA sentence is delivered via callback
 * ========================================================================= */
static void test_nmea_gga_delivered(void)
{
    /* Checksum *77 verified by Python:
     *   functools.reduce(lambda a,b: a^b,
     *     (ord(c) for c in "GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"
     *   )) == 0x77 */
    const char *sentence =
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77\r\n";

    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    test_inject(&c, (const uint8_t *)sentence, strlen(sentence));
    parse_ring(&c);

    ASSERT(ctx.count == 1,
           "GGA delivered: callback fired once");
    ASSERT(ctx.last.type == COLLECTOR_FRAME_NMEA,
           "GGA delivered: type is COLLECTOR_FRAME_NMEA");
    ASSERT(ctx.last.len == strlen(sentence),
           "GGA delivered: length matches sentence");
    ASSERT(ctx.last.data[0] == '$',
           "GGA delivered: frame starts with '$'");
    ASSERT(ctx.last.decoded.gga.valid,
           "GGA delivered: decoded.gga.valid is true");
}

/* =========================================================================
 * Test: valid RMC sentence is delivered and decoded
 * ========================================================================= */
static void test_nmea_rmc_delivered(void)
{
    /* Checksum *5A verified by Python. */
    const char *sentence =
        "$GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*5A\r\n";

    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    test_inject(&c, (const uint8_t *)sentence, strlen(sentence));
    parse_ring(&c);

    ASSERT(ctx.count == 1,
           "RMC delivered: callback fired once");
    ASSERT(ctx.last.decoded.rmc.valid,
           "RMC delivered: decoded.rmc.valid is true");
    ASSERT(ctx.last.decoded.rmc.active,
           "RMC delivered: active flag set (sentence status 'A')");
}

/* =========================================================================
 * Test: sentence with bad checksum is silently discarded
 * ========================================================================= */
static void test_nmea_bad_checksum_discarded(void)
{
    /* Correct checksum is *77 — *78 is one off. */
    const char *bad =
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*78\r\n";

    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    test_inject(&c, (const uint8_t *)bad, strlen(bad));
    parse_ring(&c);

    ASSERT(ctx.count == 0, "Bad checksum: callback not fired");
}

/* =========================================================================
 * Test: garbage bytes before a valid sentence are discarded cleanly
 * ========================================================================= */
static void test_nmea_garbage_prefix_skipped(void)
{
    const char *sentence =
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77\r\n";

    uint8_t buf[256];
    memset(buf, 0xAB, 5);                          /* 5 garbage bytes */
    memcpy(buf + 5, sentence, strlen(sentence));
    size_t total = 5 + strlen(sentence);

    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    test_inject(&c, buf, total);
    parse_ring(&c);

    ASSERT(ctx.count == 1,
           "Garbage prefix: one frame delivered");
    ASSERT(ctx.last.type == COLLECTOR_FRAME_NMEA,
           "Garbage prefix: type is NMEA");
}

/* =========================================================================
 * Test: two consecutive sentences both delivered
 * ========================================================================= */
static void test_nmea_two_sentences(void)
{
    const char *s1 =
        "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*77\r\n";
    const char *s2 =
        "$GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*5A\r\n";

    uint8_t buf[512];
    size_t  l1 = strlen(s1), l2 = strlen(s2);
    memcpy(buf,      s1, l1);
    memcpy(buf + l1, s2, l2);

    Collector   c;
    CallbackCtx ctx;
    init_test_collector(&c, &ctx);

    test_inject(&c, buf, l1 + l2);
    parse_ring(&c);

    ASSERT(ctx.count == 2, "Two sentences: both delivered");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_ring_arithmetic();
    test_nmea_gga_delivered();
    test_nmea_rmc_delivered();
    test_nmea_bad_checksum_discarded();
    test_nmea_garbage_prefix_skipped();
    test_nmea_two_sentences();

    if (g_tests_failed == 0) {
        printf("All %d collector tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d collector tests FAILED.\n",
                g_tests_failed, g_tests_run);
        return 1;
    }
}