/**
 * @file rover/station.c
 * @brief Rover station implementation (pole-top, headless).
 *
 * Opens the UM980 in rover mode and the SiK radio, then runs two
 * independent collector threads:
 *
 *   1. GNSS collector  (UM980 → NMEA):  parses GGA frames, stores the
 *      latest gm_position_t fix in RoverFixState under a mutex.
 *
 *   2. Radio collector (SiK → RTCM3):  forwards raw correction frames
 *      back to the UM980 via serial_write() to close the RTK loop.
 *
 * Both collectors receive already-open SerialPort fds via
 * collector_start_from_port() — no close/reopen cycle. On ttyAMA0,
 * closing and reopening the fd causes select() to stop firing on the
 * new fd, preventing the collector from ever receiving data.
 *
 * radio_callback writes RTCM3 corrections via um980.serial since that
 * fd remains open for the lifetime of the station.
 *
 * After collectors start, starts the TCP stream server and runs a 2 Hz
 * broadcast loop until SIGINT or SIGTERM, then cleans up in reverse order.
 */

#define _GNU_SOURCE

#include "rover/station.h"
#include "util/config.h"
#include "util/log.h"
#include "gnss/um980.h"
#include "stream/radio.h"
#include "collector/collector.h"
#include "net/stream_server.h"
#include "net/rover_packet.h"
#include "geomark.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define UI_INTERVAL_MS 500u

/* --------------------------------------------------------------------------
 * Signal handling
 * ----------------------------------------------------------------------- */

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* --------------------------------------------------------------------------
 * Shared fix state — written by GNSS callback, read by broadcast loop
 * ----------------------------------------------------------------------- */

typedef struct {
    gm_position_t   position;
    pthread_mutex_t mutex;
    int             valid;
} RoverFixState;

/* --------------------------------------------------------------------------
 * GNSS collector callback — maps NmeaGga → gm_position_t
 * ----------------------------------------------------------------------- */

static gm_fix_type_t quality_to_fix_type(uint8_t q)
{
    switch (q) {
        case 1: return FIX_SINGLE;
        case 2: return FIX_DGPS;
        case 4: return FIX_RTK_FIXED;
        case 5: return FIX_RTK_FLOAT;
        default: return FIX_NONE;
    }
}

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void gnss_callback(const CollectorFrame *frame, void *user)
{
    if (frame->type != COLLECTOR_FRAME_NMEA)
        return;

    const NmeaGga *gga = &frame->decoded.gga;
    if (!gga->valid)
        return;

    RoverFixState *state = (RoverFixState *)user;

    gm_position_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.latitude     = gga->lat;
    pos.longitude    = gga->lon;
    pos.altitude     = gga->alt_msl;
    pos.hdop         = (float)gga->hdop;
    pos.satellites   = gga->num_sats;
    pos.fix_type     = quality_to_fix_type(gga->fix_quality);
    pos.timestamp_ms = monotonic_ms();

    pthread_mutex_lock(&state->mutex);
    state->position = pos;
    state->valid    = 1;
    pthread_mutex_unlock(&state->mutex);

    log_debug("rover: fix lat=%.8f lon=%.8f alt=%.3fm hdop=%.1f sats=%u type=%d",
              pos.latitude, pos.longitude, pos.altitude,
              pos.hdop, pos.satellites, (int)pos.fix_type);
}

/* --------------------------------------------------------------------------
 * Radio collector callback — forwards RTCM3 corrections to UM980
 * ----------------------------------------------------------------------- */

typedef struct {
    Um980 *um980;
} RadioCallbackCtx;

static void radio_callback(const CollectorFrame *frame, void *user)
{
    if (frame->type != COLLECTOR_FRAME_RTCM3)
        return;

    RadioCallbackCtx *ctx = (RadioCallbackCtx *)user;

    SerialResult r = serial_write(&ctx->um980->serial, frame->data, frame->len);
    if (r != SERIAL_OK) {
        log_error("rover: serial_write RTCM3 to UM980 failed (%d), msg %d dropped",
                  r, frame->decoded.rtcm3_msg_type);
    } else {
        log_debug("rover: injected RTCM3 msg %d (%zu bytes) to UM980",
                  frame->decoded.rtcm3_msg_type, frame->len);
    }
}

/* --------------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

gm_status_t rover_station_run(const char *config_path)
{
    /* --- Config --------------------------------------------------------- */
    gm_config_t cfg;
    if (config_load(config_path, &cfg) != GM_OK) {
        log_warn("rover: config_load failed, using defaults");
        config_defaults(&cfg);
    }

    if (cfg.log_file[0] != '\0')
        log_init(cfg.log_file);

    log_info("rover: serial=%s baud=%d  radio=%s baud=%d",
             cfg.serial_device, cfg.serial_baud,
             cfg.radio_device, cfg.radio_baud);

    /* --- Fix state ------------------------------------------------------ */
    RoverFixState fix_state;
    memset(&fix_state, 0, sizeof(fix_state));
    pthread_mutex_init(&fix_state.mutex, NULL);

    /* --- UM980 ---------------------------------------------------------- */
    Um980 um980;
    memset(&um980, 0, sizeof(um980));

    SerialResult sr = um980_open(&um980, cfg.serial_device);
    if (sr != SERIAL_OK) {
        log_error("rover: um980_open(%s) failed (%d)", cfg.serial_device, sr);
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("rover: UM980 opened on %s", cfg.serial_device);

    sr = um980_init_rover(&um980);
    if (sr != SERIAL_OK) {
        log_error("rover: um980_init_rover failed (%d)", sr);
        um980_close(&um980);
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("rover: UM980 configured for rover mode");

    /* --- Radio ---------------------------------------------------------- */
    Radio radio;
    memset(&radio, 0, sizeof(radio));

    sr = radio_open(&radio, cfg.radio_device, cfg.radio_baud);
    if (sr != SERIAL_OK) {
        log_error("rover: radio_open(%s) failed (%d)", cfg.radio_device, sr);
        um980_close(&um980);
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("rover: radio opened on %s", cfg.radio_device);

    /* --- GNSS collector ------------------------------------------------- */
    Collector gnss_collector;
    memset(&gnss_collector, 0, sizeof(gnss_collector));
    gnss_collector.mode = COLLECTOR_MODE_NMEA;  /* UM980 rover outputs NMEA only */

    sr = collector_start_from_port(&gnss_collector, &um980.serial,
                                   gnss_callback, &fix_state);
    if (sr != SERIAL_OK) {
        log_error("rover: gnss collector_start_from_port failed (%d)", sr);
        radio_close(&radio);
        um980_close(&um980);
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("rover: GNSS collector running");

    /* --- Radio collector ------------------------------------------------ */
    RadioCallbackCtx radio_ctx = { .um980 = &um980 };
    Collector radio_collector;
    memset(&radio_collector, 0, sizeof(radio_collector));
    radio_collector.mode = COLLECTOR_MODE_RTCM3;  /* SiK radio delivers RTCM3 only */

    sr = collector_start_from_port(&radio_collector, &radio.serial,
                                   radio_callback, &radio_ctx);
    if (sr != SERIAL_OK) {
        log_error("rover: radio collector_start_from_port failed (%d)", sr);
        collector_stop(&gnss_collector);
        radio_close(&radio);
        um980_close(&um980);
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("rover: radio collector running");

    /* --- Stream server -------------------------------------------------- */
    gm_status_t ss = stream_server_start();
    if (ss != GM_OK) {
        log_warn("rover: stream_server_start failed — running without WiFi stream");
    } else {
        log_info("rover: stream server listening on port %d", ROVER_PACKET_PORT);
    }

    log_info("rover: RTK loop active — waiting for fixes");

    /* --- Signal handling ------------------------------------------------ */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* --- Broadcast loop at 2 Hz ---------------------------------------- */
    while (g_running) {
        gm_position_t snap;
        int valid;
        pthread_mutex_lock(&fix_state.mutex);
        snap  = fix_state.position;
        valid = fix_state.valid;
        pthread_mutex_unlock(&fix_state.mutex);

        if (ss == GM_OK) {
            RoverStatusPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.magic        = ROVER_PACKET_MAGIC;
            pkt.lat          = snap.latitude;
            pkt.lon          = snap.longitude;
            pkt.alt_msl      = snap.altitude;
            pkt.hdop         = snap.hdop;
            pkt.fix_quality  = (uint8_t)snap.fix_type;
            pkt.num_sats     = snap.satellites;
            pkt.age_of_fix_s = (snap.timestamp_ms > 0)
                               ? (uint16_t)((monotonic_ms() - snap.timestamp_ms) / 1000u)
                               : 0u;
            pkt.valid        = (uint8_t)valid;
            stream_server_broadcast(&pkt);
        }

        usleep(UI_INTERVAL_MS * 1000u);
    }

    log_info("rover: shutting down");

    /* --- Cleanup (reverse init order) ---------------------------------- */
    if (ss == GM_OK)
        stream_server_stop();
    collector_stop(&radio_collector);
    collector_stop(&gnss_collector);
    radio_close(&radio);
    um980_close(&um980);
    pthread_mutex_destroy(&fix_state.mutex);

    log_info("rover: shutdown complete");
    return GM_OK;
}