#define _GNU_SOURCE

#include "collector.h"
#include "gnss/nmea.h"
#include "util/log.h"

#include <stddef.h>
#include <string.h>

/* =========================================================================
 * Ring buffer helpers
 *
 * COLLECTOR_RING_SIZE is a power of two — all index arithmetic uses
 * bitwise AND instead of modulo.
 * ========================================================================= */

#define RING_MASK ((size_t)(COLLECTOR_RING_SIZE - 1))

static inline size_t ring_used(const Collector *c) {
    return (c->head - c->tail) & RING_MASK;
}

static inline size_t ring_free(const Collector *c) {
    /* One slot kept empty so head == tail unambiguously means "empty". */
    return COLLECTOR_RING_SIZE - 1 - ring_used(c);
}

static inline int ring_empty(const Collector *c) {
    return c->head == c->tail;
}

/* Write one byte. Returns 1 on success, 0 if the buffer is full. */
static int ring_push(Collector *c, uint8_t byte) {
    if (ring_free(c) == 0)
        return 0;
    c->ring[c->head & RING_MASK] = byte;
    c->head = (c->head + 1) & RING_MASK;
    return 1;
}

/* Read byte at 'offset' positions ahead of tail, without consuming.
 * Caller must guarantee: offset < ring_used(c). */
static inline uint8_t ring_peek(const Collector *c, size_t offset) {
    return c->ring[(c->tail + offset) & RING_MASK];
}

/* Advance tail by 'n', discarding those bytes. */
static inline void ring_discard(Collector *c, size_t n) {
    c->tail = (c->tail + n) & RING_MASK;
}

/* Copy 'n' bytes from tail into dst without advancing tail.
 * Handles wrap-around transparently.
 * Caller must guarantee: n <= ring_used(c). */
static void ring_copy_to(const Collector *c, uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = ring_peek(c, i);
}

/* =========================================================================
 * NMEA frame parser
 *
 * Returns:
 *   > 0   bytes consumed (complete frame dispatched via callback)
 *     0   not enough data yet — keep buffering
 *   < 0   bad framing at current tail — caller discards 1 byte and retries
 * ========================================================================= */
static int try_nmea(Collector *c) {
    size_t avail = ring_used(c);

    /* Shortest valid NMEA sentence: "$XX*HH\r\n" = 9 bytes */
    if (avail < 9)
        return 0;

    if (ring_peek(c, 0) != '$')
        return -1;

    /* Scan for '\r\n' terminator. */
    size_t sentence_len = 0;
    for (size_t i = 1; i + 1 < avail; i++) {
        if (ring_peek(c, i) == '\r' && ring_peek(c, i + 1) == '\n') {
            sentence_len = i + 2; /* total bytes including '\r\n' */
            break;
        }
    }

    if (sentence_len == 0) {
        /* No terminator yet.  If we already hold a full frame's worth of
         * data with no '\r\n', the '$' is garbage — discard it. */
        if (avail >= COLLECTOR_FRAME_MAX)
            return -1;
        return 0;
    }

    if (sentence_len > COLLECTOR_FRAME_MAX)
        return -1;

    /* Linearise into a stack buffer for checksum validation and parsing. */
    uint8_t buf[COLLECTOR_FRAME_MAX + 1];
    ring_copy_to(c, buf, sentence_len);
    buf[sentence_len] = '\0';

    if (!nmea_checksum_valid((const char *)buf))
        return -1;

    /* Build the frame. */
    CollectorFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = COLLECTOR_FRAME_NMEA;
    memcpy(frame.data, buf, sentence_len);
    frame.len = sentence_len;

    if (nmea_parse_gga((const char *)buf, &frame.decoded.gga)) {
        /* decoded.gga is valid */
    } else if (nmea_parse_rmc((const char *)buf, &frame.decoded.rmc)) {
        /* decoded.rmc is valid */
    }

    c->callback(&frame, c->user);
    return (int)sentence_len;
}

/* =========================================================================
 * RTCM3 frame parser
 *
 * Returns:
 *   > 0   bytes consumed (complete frame dispatched via callback)
 *     0   not enough data yet
 *   < 0   bad framing at current tail
 * ========================================================================= */
static int try_rtcm3(Collector *c)
{
    size_t avail = ring_used(c);

    if (avail < RTCM3_MIN_FRAME_LEN)
        return 0;

    uint8_t buf[COLLECTOR_RING_SIZE];
    ring_copy_to(c, buf, avail);

    size_t frame_start = 0;
    size_t payload_len = 0;

    int found = rtcm3_find_frame(buf, avail, &frame_start, &payload_len);

    if (!found) {
        if (buf[0] != RTCM3_PREAMBLE)
            return -1;
        return 0;
    }

    if (frame_start > 0) {
        ring_discard(c, frame_start);
        return 0;
    }

    size_t frame_len = 3 + payload_len + 3;

    if (frame_len > COLLECTOR_FRAME_MAX)
        return -1;

    if (frame_len > avail)
        return 0;

    CollectorFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = COLLECTOR_FRAME_RTCM3;
    memcpy(frame.data, buf, frame_len);
    frame.len = frame_len;

    const uint8_t *payload = buf + 3;
    frame.decoded.rtcm3_msg_type = rtcm3_decode(payload, payload_len);

    c->callback(&frame, c->user);
    return (int)frame_len;
}

/* =========================================================================
 * Main parse loop
 * ========================================================================= */
void parse_ring(Collector *c)
{
    while (!ring_empty(c)) {
        uint8_t lead = ring_peek(c, 0);
        int     consumed;

        if (lead == '$') {
            consumed = try_nmea(c);
        } else if (lead == RTCM3_PREAMBLE) {
            consumed = try_rtcm3(c);
        } else {
            ring_discard(c, 1);
            continue;
        }

        if (consumed > 0) {
            ring_discard(c, (size_t)consumed);
        } else if (consumed < 0) {
            ring_discard(c, 1);
        } else {
            break;
        }
    }
}

/* =========================================================================
 * Collector thread
 * ========================================================================= */
static void *collector_thread(void *arg)
{
    Collector *c = (Collector *)arg;
    uint8_t    tmp[256];

    while (c->running) {
        int n = serial_read(&c->serial, tmp, sizeof(tmp));

        if (n > 0) {
            log_debug("collector: received %d bytes, lead=0x%02x", n, tmp[0]); /* TEMP */
            for (int i = 0; i < n; i++) {
                if (!ring_push(c, tmp[i])) {
                    break;
                }
            }
            parse_ring(c);
        } else if (n == -(int)SERIAL_ERR_TIMEOUT) {
            continue;
        } else if (n < 0) {
            break;
        }
    }

    return NULL;
}

/* =========================================================================
 * Internal thread start helper
 * ========================================================================= */
static SerialResult start_thread(Collector *c, CollectorCallback callback, void *user)
{
    c->callback = callback;
    c->user     = user;
    c->head     = 0;
    c->tail     = 0;
    c->running  = 1;

    if (pthread_create(&c->thread, NULL, collector_thread, c) != 0) {
        c->running = 0;
        return SERIAL_ERR_IO;
    }

    return SERIAL_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

SerialResult collector_start(Collector *c, const char *device, int baud,
                             CollectorCallback callback, void *user)
{
    memset(c, 0, sizeof(*c));
    c->owns_port = 1;

    SerialResult r = serial_open(&c->serial, device, baud, 100 /* ms timeout */);
    if (r != SERIAL_OK)
        return r;

    r = start_thread(c, callback, user);
    if (r != SERIAL_OK) {
        serial_close(&c->serial);
        return r;
    }

    return SERIAL_OK;
}

SerialResult collector_start_from_port(Collector *c, SerialPort *port,
                                       CollectorCallback callback, void *user)
{
    if (!c || !port || port->fd == -1 || !callback)
        return SERIAL_ERR_ARG;

    memset(c, 0, sizeof(*c));
    c->serial    = *port;   /* copy the SerialPort struct (fd, baud, timeout) */
    c->owns_port = 0;       /* caller owns the fd — we must not close it */

    return start_thread(c, callback, user);
}

void collector_stop(Collector *c)
{
    if (!c->running)
        return;
    c->running = 0;
    pthread_join(c->thread, NULL);
    if (c->owns_port)
        serial_close(&c->serial);
}