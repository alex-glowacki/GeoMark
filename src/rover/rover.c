/**
 * @file rover/rover.c
 * @brief Rover logic: receive RTCM3 corrections from the base, feed them
 *        back to the UM980, and accumulate RTK position fixes.
 *
 * Data flow:
 *   UM980 UART  → gnss_collector  (NMEA)  → stores latest gm_position_t fix
 *   SiK radio   → radio_collector (RTCM3) → serial_write() back to UM980
 *
 * Two independent collector threads run simultaneously.  The latest fix is
 * stored in a shared struct protected by a mutex, ready for the UI layer
 * to read via rover_get_fix().
 *
 * The Um980 and Radio structs are used only for init command exchange.
 * After init, both fds are closed so the collectors own their respective
 * ports exclusively — two fds open on the same device splits the byte
 * stream and prevents frame detection.
 *
 * rover_station_run() blocks until SIGINT or SIGTERM.
 */

#define _GNU_SOURCE

#include "rover/station.h"
#include "util/config.h"
#include "util/log.h"
#include "gnss/um980.h"
#include "stream/radio.h"
#include "collector/collector.h"
#include "geomark.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Signal handling
 * ---------------------------------------------------------------------- */

static volatile sig_atomic_t g_stop = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_stop = 1;
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* --------------------------------------------------------------------------
 * Shared fix state (written by gnss_collector thread, read by UI)
 * ---------------------------------------------------------------------- */

typedef struct {
    gm_position_t position;
    int           valid;
    pthread_mutex_t mutex;
} RoverFixState;

static void fix_state_init(RoverFixState *fs) {
    memset(fs, 0, sizeof(*fs));
    pthread_mutex_init(&fs->mutex, NULL);
}

static void fix_state_destroy(RoverFixState *fs) {
    pthread_mutex_destroy(&fs->mutex);
}

/* --------------------------------------------------------------------------
 * GNSS collector callback — fires when UM980 emits a NMEA sentence
 * ---------------------------------------------------------------------- */

typedef struct {
    RoverFixState *fix_state;
} GnssCallbackCtx;

static void gnss_callback(const CollectorFrame *frame, void *user) {
    GnssCallbackCtx *ctx = (GnssCallbackCtx *)user;

    if (frame->type != COLLECTOR_FRAME_NMEA)
        return;

    if (!frame->decoded.gga.valid)
        return;

    const NmeaGga *gga = &frame->decoded.gga;

    gm_position_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.latitude   = gga->lat;
    pos.longitude  = gga->lon;
    pos.altitude   = gga->alt_msl;
    pos.hdop       = (float)gga->hdop;
    pos.satellites = gga->num_sats;

    switch (gga->fix_quality) {
        case 1:  pos.fix_type = FIX_SINGLE;    break;
        case 2:  pos.fix_type = FIX_DGPS;      break;
        case 4:  pos.fix_type = FIX_RTK_FIXED; break;
        case 5:  pos.fix_type = FIX_RTK_FLOAT; break;
        default: pos.fix_type = FIX_NONE;       break;
    }

    pthread_mutex_lock(&ctx->fix_state->mutex);
    ctx->fix_state->position = pos;
    ctx->fix_state->valid    = 1;
    pthread_mutex_unlock(&ctx->fix_state->mutex);

    log_debug("rover: fix type=%d lat=%.8f lon=%.8f alt=%.3f sats=%d hdop=%.1f",
              pos.fix_type, pos.latitude, pos.longitude,
              pos.altitude, pos.satellites, (double)pos.hdop);
}

/* --------------------------------------------------------------------------
 * Radio collector callback — fires when SiK radio delivers an RTCM3 frame.
 *
 * Writes the correction frame directly to the gnss_collector's serial fd
 * so the UM980 receives it.  The gnss_collector owns the only open fd to
 * /dev/ttyAMA0 after um980_close() — we write through its SerialPort.
 * ---------------------------------------------------------------------- */

typedef struct {
    Collector *gnss_collector; /* write RTCM3 corrections via this fd */
} RadioCallbackCtx;

static void radio_callback(const CollectorFrame *frame, void *user) {
    RadioCallbackCtx *ctx = (RadioCallbackCtx *)user;

    if (frame->type != COLLECTOR_FRAME_RTCM3)
        return;

    SerialResult r = serial_write(&ctx->gnss_collector->serial,
                                  frame->data, frame->len);
    if (r != SERIAL_OK) {
        log_error("rover: serial_write to UM980 failed (%d) — RTCM3 msg %d dropped",
                  r, frame->decoded.rtcm3_msg_type);
    } else {
        log_debug("rover: forwarded RTCM3 msg %d (%zu bytes) to UM980",
                  frame->decoded.rtcm3_msg_type, frame->len);
    }
}

/* --------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------- */

gm_status_t rover_station_run(const char *config_path) {
    /* --- Configuration -------------------------------------------------- */
    gm_config_t cfg;
    config_defaults(&cfg);

    gm_status_t cs = config_load(config_path, &cfg);
    if (cs != GM_OK) {
        log_warn("rover: could not load config from %s (status %d), using defaults",
                 config_path, cs);
    }

    log_init(cfg.log_file[0] ? cfg.log_file : NULL);
    log_info("rover: config loaded — gnss=%s radio=%s",
             cfg.serial_device, cfg.radio_device);

    /* --- UM980 init ----------------------------------------------------- */
    Um980 um980;
    memset(&um980, 0, sizeof(um980));

    SerialResult sr = um980_open(&um980, cfg.serial_device);
    if (sr != SERIAL_OK) {
        log_error("rover: um980_open(%s) failed (%d)", cfg.serial_device, sr);
        return GM_ERR_IO;
    }
    log_info("rover: UM980 opened on %s", cfg.serial_device);

    sr = um980_init_rover(&um980);
    if (sr != SERIAL_OK) {
        log_error("rover: um980_init_rover failed (%d)", sr);
        um980_close(&um980);
        return GM_ERR_IO;
    }
    log_info("rover: UM980 configured for rover mode");

    /* Close init fd — gnss_collector takes exclusive ownership of the port. */
    um980_close(&um980);
    log_info("rover: UM980 init fd closed — gnss_collector takes over");

    /* --- Radio init ----------------------------------------------------- */
    Radio radio;
    memset(&radio, 0, sizeof(radio));

    sr = radio_open(&radio, cfg.radio_device, cfg.radio_baud);
    if (sr != SERIAL_OK) {
        log_error("rover: radio_open(%s) failed (%d)", cfg.radio_device, sr);
        return GM_ERR_IO;
    }
    log_info("rover: radio opened on %s at %d baud",
             cfg.radio_device, cfg.radio_baud);

    /* Close init fd — radio_collector takes exclusive ownership of the port. */
    radio_close(&radio);
    log_info("rover: radio init fd closed — radio_collector takes over");

    /* --- Shared fix state ----------------------------------------------- */
    RoverFixState fix_state;
    fix_state_init(&fix_state);

    /* --- GNSS collector (UM980 → NMEA fixes) ---------------------------- */
    GnssCallbackCtx gnss_ctx;
    gnss_ctx.fix_state = &fix_state;

    Collector gnss_collector;
    memset(&gnss_collector, 0, sizeof(gnss_collector));

    sr = collector_start(&gnss_collector,
                         cfg.serial_device, cfg.serial_baud,
                         gnss_callback, &gnss_ctx);
    if (sr != SERIAL_OK) {
        log_error("rover: gnss collector_start failed (%d)", sr);
        fix_state_destroy(&fix_state);
        return GM_ERR_IO;
    }
    log_info("rover: GNSS collector running");

    /* --- Radio collector (SiK → RTCM3 → UM980) ------------------------- */
    /* radio_callback writes corrections to gnss_collector's serial fd,
     * which is the only open fd to /dev/ttyAMA0 at this point. */
    RadioCallbackCtx radio_ctx;
    radio_ctx.gnss_collector = &gnss_collector;

    Collector radio_collector;
    memset(&radio_collector, 0, sizeof(radio_collector));

    sr = collector_start(&radio_collector,
                         cfg.radio_device, cfg.radio_baud,
                         radio_callback, &radio_ctx);
    if (sr != SERIAL_OK) {
        log_error("rover: radio collector_start failed (%d)", sr);
        collector_stop(&gnss_collector);
        fix_state_destroy(&fix_state);
        return GM_ERR_IO;
    }
    log_info("rover: radio collector running — RTK correction loop active");

    /* --- Main loop ------------------------------------------------------ */
    install_signal_handlers();

    while (!g_stop) {
        pause();
    }

    log_info("rover: shutdown signal received");

    /* --- Cleanup (reverse init order) ---------------------------------- */
    collector_stop(&radio_collector);
    log_info("rover: radio collector stopped");

    collector_stop(&gnss_collector);
    log_info("rover: GNSS collector stopped");

    fix_state_destroy(&fix_state);

    return GM_OK;
}