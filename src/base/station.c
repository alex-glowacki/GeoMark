/**
 * @file base/station.c
 * @brief Base station implementation.
 *
 * Opens the UM980 in base mode, opens the SiK radio, then runs a
 * collector on the UM980 serial port.  Every RTCM3 frame received
 * from the UM980 is forwarded out over the radio to the rover.
 * Blocks until SIGINT or SIGTERM, then cleans up in reverse order.
 *
 * The Um980 fd is closed after init so the collector owns the port
 * exclusively — two fds open on the same device splits the byte stream
 * and prevents RTCM3 frame detection.
 *
 * A 500 ms settle delay is inserted after um980_close() so the kernel
 * UART driver quiesces before the collector opens its own fd. Without
 * this, select() on the new fd never fires even though data is present.
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
#include <unistd.h>

/* --------------------------------------------------------------------------
 * Module-level state shared with signal handler and callback
 * ----------------------------------------------------------------------- */

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* --------------------------------------------------------------------------
 * Collector callback — forwards RTCM3 frames out over the radio
 * ----------------------------------------------------------------------- */

typedef struct {
    Radio *radio;
} BaseCallbackCtx;

static void base_callback(const CollectorFrame *frame, void *user)
{
    if (frame->type != COLLECTOR_FRAME_RTCM3) {
        return;
    }

    BaseCallbackCtx *ctx = (BaseCallbackCtx *)user;
    SerialResult r = radio_write(ctx->radio, frame->data, frame->len);
    if (r != SERIAL_OK) {
        log_error("base: radio_write failed (%d), RTCM3 msg %d dropped",
                  r, frame->decoded.rtcm3_msg_type);
    } else {
        log_debug("base: forwarded RTCM3 msg %d (%zu bytes)",
                  frame->decoded.rtcm3_msg_type, frame->len);
    }
}

/* --------------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

gm_status_t base_station_run(const char *config_path)
{
    /* --- Config --------------------------------------------------------- */
    gm_config_t cfg;
    if (config_load(config_path, &cfg) != GM_OK) {
        log_warn("base: config_load failed, using defaults");
        config_defaults(&cfg);
    }

    if (cfg.log_file[0] != '\0') {
        log_init(cfg.log_file);
    }

    log_info("base: serial=%s baud=%d  radio=%s baud=%d",
             cfg.serial_device, cfg.serial_baud,
             cfg.radio_device, cfg.radio_baud);

    /* --- UM980 ---------------------------------------------------------- */
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

    /* Close the init fd before starting the collector — two fds open on
     * the same device splits the byte stream and prevents frame detection.
     * Wait 500 ms after closing so the kernel UART driver quiesces and
     * the collector's select() fires correctly on the new fd. */
    um980_close(&um980);
    log_info("base: UM980 init fd closed — settling 500ms");
    usleep(500000);
    log_info("base: collector takes over");

    /* --- Radio ---------------------------------------------------------- */
    Radio radio;
    memset(&radio, 0, sizeof(radio));

    sr = radio_open(&radio, cfg.radio_device, cfg.radio_baud);
    if (sr != SERIAL_OK) {
        log_error("base: radio_open(%s) failed (%d)", cfg.radio_device, sr);
        return GM_ERR_IO;
    }
    log_info("base: radio opened on %s", cfg.radio_device);

    /* --- Collector ------------------------------------------------------- */
    BaseCallbackCtx ctx = { .radio = &radio };
    Collector collector;
    memset(&collector, 0, sizeof(collector));

    sr = collector_start(&collector, cfg.serial_device, cfg.serial_baud,
                         base_callback, &ctx);
    if (sr != SERIAL_OK) {
        log_error("base: collector_start failed (%d)", sr);
        radio_close(&radio);
        return GM_ERR_IO;
    }
    log_info("base: collector running — relaying RTCM3 to radio");

    /* --- Signal handling ------------------------------------------------- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (g_running) {
        pause();
    }

    log_info("base: shutting down");

    /* --- Cleanup (reverse init order) ----------------------------------- */
    collector_stop(&collector);
    radio_close(&radio);

    log_info("base: shutdown complete");
    return GM_OK;
}