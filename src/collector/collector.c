#include "gnss/nmea.h"
#include <stddef.h>
#define _GNU_SOURCE

#include "collector.h"

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

    /* Decode into the appropriate union member.
     * Unrecognised sentence types are forwarded raw; decoded union stays
     * zeroed (NmeaGga/NmeaRmc both have a 'valid' flag — caller checks it). */
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
 * Linearises the ring into a scratch buffer, then delegates to
 * rtcm3_find_frame() for preamble detection, length extraction, and
 * CRC-24Q validation.
 *
 * rtcm3_find_frame() signature (from rtcm3.h):
 *   int rtcm3_find_frame(const uint8_t *buf, size_t buf_len,
 *                        size_t *frame_start, size_t *payload_len);
 *   Returns 1 if a valid frame was found, 0 otherwise.
 *
 * Returns:
 *   > 0   bytes consumed (complete frame dispatched via callback)
 *     0   not enough data yet
 *   < 0   bad framing at current tail
 * ========================================================================= */
static int try_rtcm3(Collector *c)
{
    size_t avail = ring_used(c);

    /* Minimum RTCM3 frame: 3-byte header + 0-byte payload + 3-byte CRC */
    if (avail < RTCM3_MIN_FRAME_LEN)
        return 0;

    /* Linearise the ring into a contiguous scratch buffer. */
    uint8_t buf[COLLECTOR_RING_SIZE];
    ring_copy_to(c, buf, avail);

    size_t frame_start = 0;
    size_t payload_len = 0;

    int found = rtcm3_find_frame(buf, avail, &frame_start, &payload_len);

    if (!found) {
        /* No valid frame found in current buffer contents.
         * If the buffer starts with 0xD3 but is just incomplete, wait.
         * If it doesn't start with 0xD3 at all, discard the lead byte. */
        if (buf[0] != RTCM3_PREAMBLE)
            return -1;
        return 0;
    }

    /* A valid frame was found.  It may not start at index 0 — there could
     * be garbage bytes before the preamble.  Discard the garbage first so
     * next time around the ring tail points directly at 0xD3. */
    if (frame_start > 0) {
        ring_discard(c, frame_start);
        return 0; /* re-enter parse_ring on next iteration */
    }

    /* Frame starts at index 0.  Total frame size:
     *   3-byte header + payload_len bytes + 3-byte CRC */
    size_t frame_len = 3 + payload_len + 3;

    if (frame_len > COLLECTOR_FRAME_MAX)
        return -1;

    /* Ensure the full frame is actually present in our linearised buffer. */
    if (frame_len > avail)
        return 0;

    CollectorFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = COLLECTOR_FRAME_RTCM3;
    memcpy(frame.data, buf, frame_len);
    frame.len = frame_len;

    /* Payload starts immediately after the 3-byte header. */
    const uint8_t *payload = buf + 3;
    frame.decoded.rtcm3_msg_type = rtcm3_decode(payload, payload_len);

    c->callback(&frame, c->user);
    return (int)frame_len;
}

/* =========================================================================
 * Main parse loop
 *
 * Drains as many complete frames from the ring as possible before
 * returning to wait for more serial data.
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
            /* Unrecognised lead byte — not a valid frame start, discard. */
            ring_discard(c, 1);
            continue;
        }

        if (consumed > 0) {
            ring_discard(c, (size_t)consumed);
        } else if (consumed < 0) {
            /* Bad framing — discard the lead byte and retry. */
            ring_discard(c, 1);
        } else {
            /* consumed == 0: need more data from the serial port. */
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
            for (int i = 0; i < n; i++) {
                if (!ring_push(c, tmp[i])) {
                    /* Ring full — parser is falling behind.  Drop the rest
                     * of this burst; re-sync will happen on the next valid
                     * preamble byte. */
                    break;
                }
            }
            parse_ring(c);
        } else if (n == -(int)SERIAL_ERR_TIMEOUT) {
            /* No data within the timeout window — normal idle, keep looping. */
            continue;
        } else if (n < 0) {
            /* Real I/O error on the port — stop the thread. */
            break;
        }
    }

    return NULL;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

SerialResult collector_start(Collector        *c,
                             const char       *device,
                             int               baud,
                             CollectorCallback callback,
                             void             *user)
{
    memset(c, 0, sizeof(*c));
    c->callback = callback;
    c->user     = user;
    /* head and tail initialise to 0 via memset — ring starts empty. */

    SerialResult r = serial_open(&c->serial, device, baud, 100 /* ms timeout */);
    if (r != SERIAL_OK)
        return r;

    c->running = 1;
    if (pthread_create(&c->thread, NULL, collector_thread, c) != 0) {
        serial_close(&c->serial);
        c->running = 0;
        return SERIAL_ERR_IO;
    }

    return SERIAL_OK;
}

void collector_stop(Collector *c)
{
    if (!c->running)
        return;
    c->running = 0;
    pthread_join(c->thread, NULL);
    serial_close(&c->serial);
}