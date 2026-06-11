#ifndef GEOMARK_COLLECTOR_H
#define GEOMARK_COLLECTOR_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "../gnss/nmea.h"
#include "../gnss/rtcm3.h"
#include "../stream/serial.h"

/* Maximum bytes in any single frame we will ever buffer.
 * RTCM3 maximum: 3-byte header + 1023-byte payload + 3-byte CRC = 1029 bytes.
 * NMEA maximum defined by NMEA 0183 standard: 82 bytes — well within limit. */
#define COLLECTOR_FRAME_MAX 1029

/* Ring buffer capacity.  MUST be a power of two (enables bitmask wrapping). */
#define COLLECTOR_RING_SIZE 4096

typedef enum {
    COLLECTOR_FRAME_NMEA = 0,
    COLLECTOR_FRAME_RTCM3 = 1,
} CollectorFrameType;

/* A single complete, validated frame ready for the application layer.
 *
 *   type == COLLECTOR_FRAME_NMEA
 *     data  : null-terminated ASCII sentence, includes '$' and '\r\n'
 *     decoded.gga populated if sentence is GGA; decoded.rmc if RMC.
 *     Both structs carry a 'valid' flag — check it before use.
 *
 *   type == COLLECTOR_FRAME_RTCM3
 *     data  : raw binary frame (header + payload + CRC)
 *     decoded.rtcm3_msg_type : message type number (e.g. 1005), or -1. */
typedef struct {
    CollectorFrameType type;
    uint8_t data[COLLECTOR_FRAME_MAX];
    size_t len;
    union {
        NmeaGga gga;
        NmeaRmc rmc;
        int rtcm3_msg_type;
    } decoded;
} CollectorFrame;

/* Called on the collector thread for every complete, validated frame.
 * 'frame' is only valid for the duration of this call — copy if needed.
 * 'user'  is the pointer passed to collector_start(). */
typedef void (*CollectorCallback)(const CollectorFrame *frame, void *user);

typedef struct {
    SerialPort serial;
    CollectorCallback callback;
    void *user;

    /* Circular byte buffer.
     * head == tail means empty.  head advances on write; tail on consume. */
    uint8_t ring[COLLECTOR_RING_SIZE];
    size_t head;
    size_t tail;

    /* Pthread handle and stop flag. */
    pthread_t thread;
    volatile int running;
} Collector;

/* Exposed for unit testing only — not part of the public API. */
void parse_ring(Collector *c);

/* Open the serial port at 'device'/'baud' and start the collector thread.
 * 'callback' is invoked (on the collector thread) for every complete frame.
 * 'user' is passed through unchanged to each callback invocation.
 * Returns SERIAL_OK on success, or a SerialResult error code on failure. */
SerialResult collector_start(Collector *c, const char *device, int baud, CollectorCallback callback,
                             void *user);

/* Signal the thread to exit and block until it does, then close the port.
 * Safe to call even if collector_start() returned an error. */
void collector_stop(Collector *c);

#endif /* GEOMARK_COLLECTOR_H */