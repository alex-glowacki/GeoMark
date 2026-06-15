/**
 * @file base/base.c
 * @brief Base station logic: configure UM980 for base mode, relay RTCM3
 *        correction frames out the SiK radio.
 *
 * Data flow:
 *   UM980 UART → collector (RTCM3 frame detection) → radio_write()
 *
 * The collector thread handles all I/O. base_station_run() blocks in a
 * signal-wait loop and returns only after SIGINT or SIGTERM.
 */

#define _GNU_SOURCE

#include "base/station.h"
#include "util/config.h"
#include "util/log.h"
#include "gnss/um980.h"
#include "stream/radio.h"
#include "collector/collector.h"

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
 * Collector callback — fires on the collector thread
 * ---------------------------------------------------------------------- */

typedef struct {
    Radio *radio;
} BaseCallbackCtx;

static void base_callback(const CollectorFrame *frame, void *user) {
    BaseCallbackCtx *ctx = (BaseCallbackCtx *)user;

    if (frame->type != COLLECTOR_FRAME_RTCM3) {
        return; /* base station ignores NMEA output from the UM980 */
    }

    SerialResult r = radio_write(ctx->radio, frame->data, frame->len);
    if (r != SERIAL_OK) {
        log_error("base: radio_write failed (%d) — RTCM3 msg %d dropped",
                  r, frame->decoded.rtcm3_msg_type);
    } else {
        log_debug("base: forwarded RTCM3 msg %d (%zu bytes)",
                  frame->decoded.rtcm3_msg_type, frame->len);
    }
}

/* --------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------- */

gm_status_t base_station_run(const char *config_path) {
    /* --- Configuration -------------------------------------------------- */
    gm_config_t cfg;
    config_defaults(&cfg);

    gm_status_t cs = config_load(config_path, &cfg);
    if (cs != GM_OK) {
        log_warn("base: could not load config from %s (status %d), using defaults",
                 config_path, cs);
    }

    log_init(cfg.log_file[0] ? cfg.log_file : NULL);
    log_info("base: config loaded — gnss=%s radio=%s",
             cfg.serial_device, cfg.radio_device);

    /* --- Hardware init -------------------------------------------------- */
    Um980 um980;
    memset(&um980, 0, sizeof(um980));

    SerialResult sr = um980_open(&um980, cfg.serial_device);
    if (sr != SERIAL_OK) {
        log_error("base: um980_open(%s) failed (%d)", cfg.serial_device, sr);
        return GM_ERR_IO;
    }
    log_info("base: UM980 opened on %s", cfg.serial_device);

    sr = um980_init_base(&um980);
    if (sr != SERIAL_OK) {
        log_error("base: um980_init_base failed (%d)", sr);
        um980_close(&um980);
        return GM_ERR_IO;
    }
    log_info("base: UM980 configured for base mode");

    /* Close the init fd — the collector opens its own fd for the data stream.
     * Both holding the port simultaneously splits the byte stream and prevents
     * the collector from seeing complete RTCM3 frames. */
    um980_close(&um980);
    log_info("base: UM980 init fd closed — collector takes over");

    Radio radio;
    memset(&radio, 0, sizeof(radio));

    sr = radio_open(&radio, cfg.radio_device, cfg.radio_baud);
    if (sr != SERIAL_OK) {
        log_error("base: radio_open(%s) failed (%d)", cfg.radio_device, sr);
        return GM_ERR_IO;
    }
    log_info("base: radio opened on %s at %d baud",
             cfg.radio_device, cfg.radio_baud);

    /* --- Collector ------------------------------------------------------- */
    BaseCallbackCtx ctx;
    ctx.radio = &radio;

    Collector collector;
    memset(&collector, 0, sizeof(collector));

    sr = collector_start(&collector,
                         cfg.serial_device, cfg.serial_baud,
                         base_callback, &ctx);
    if (sr != SERIAL_OK) {
        log_error("base: collector_start failed (%d)", sr);
        radio_close(&radio);
        return GM_ERR_IO;
    }
    log_info("base: collector running — relaying RTCM3 to radio");

    /* --- Main loop ------------------------------------------------------ */
    install_signal_handlers();

    while (!g_stop) {
        pause();
    }

    log_info("base: shutdown signal received");

    /* --- Cleanup -------------------------------------------------------- */
    collector_stop(&collector);
    log_info("base: collector stopped");

    radio_close(&radio);
    log_info("base: radio closed");

    return GM_OK;
}