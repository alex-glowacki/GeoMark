/**
 * @file ui/client.c
 * @brief UI client mode implementation for the Pi 5 handheld.
 *
 * Wires together:
 *   stream_client  →  packet callback  →  RoverFixState
 *   2 Hz loop      →  screen_update()  ←  RoverFixState
 */

#define _GNU_SOURCE

#include "ui/client.h"
#include "net/stream_client.h"
#include "net/rover_packet.h"
#include "ui/tft/display.h"
#include "ui/tft/screen.h"
#include "util/log.h"
#include "geomark.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * TFT hardware — Pi 5 wiring (same physical connections as Zero 2 W had)
 * ----------------------------------------------------------------------- */

#define UI_SPI_DEVICE  "/dev/spidev0.0"
#define UI_DC_GPIO     24
#define UI_RST_GPIO    25

/* UI refresh: 500 ms = 2 Hz */
#define UI_INTERVAL_MS  500u

/* --------------------------------------------------------------------------
 * Signal handling
 * ----------------------------------------------------------------------- */

static volatile int g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* --------------------------------------------------------------------------
 * Shared fix state — written by packet callback, read by render loop
 * ----------------------------------------------------------------------- */

typedef struct {
    gm_position_t position;
    pthread_mutex_t mutex;
    int valid;
} UIFixState;

/* --------------------------------------------------------------------------
 * Packet callback — maps RoverStatusPacket → gm_position_t
 * ----------------------------------------------------------------------- */

static void packet_callback(const RoverStatusPacket *pkt, void *user) {
    UIFixState *state = (UIFixState *)user;

    if (!pkt->valid)
        return;

    gm_position_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.latitude = pkt->lat;
    pos.longitude = pkt->lon;
    pos.altitude = pkt->alt_msl;
    pos.hdop = (float)pkt->hdop;
    pos.satellites = pkt->num_sats;
    pos.fix_type = (gm_fix_type_t)pkt->fix_quality;
    pos.timestamp_ms = 0;   /* age calculated server-side; see age_of_fix_s */

    pthread_mutex_lock(&state->mutex);
    state->position = pos;
    state->valid = 1;
    pthread_mutex_unlock(&state->mutex);
}

/* --------------------------------------------------------------------------
 * Monotonic clock helper
 * ----------------------------------------------------------------------- */

static uint32_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* --------------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

gm_status_t ui_client_run(const char *pole_top_host) {
    /* --- TFT display ---------------------------------------------------- */
    gm_status_t ds = display_open(UI_SPI_DEVICE, UI_DC_GPIO, UI_RST_GPIO);
    if (ds != GM_OK) {
        log_error("ui_client: display_open failed — cannot run without display");
        return GM_ERR_IO;
    }
    screen_init();
    log_info("ui_client: TFT display ready");

    /* --- Fix state ------------------------------------------------------ */
    UIFixState fix_state;
    memset(&fix_state, 0, sizeof(fix_state));
    pthread_mutex_init(&fix_state.mutex, NULL);

    /* --- Stream client -------------------------------------------------- */
    gm_status_t cs = stream_client_start(pole_top_host, packet_callback, &fix_state);
    if (cs != GM_OK) {
        log_error("ui_client: stream_client_start failed");
        display_close();
        pthread_mutex_destroy(&fix_state.mutex);
        return GM_ERR_IO;
    }
    log_info("ui_client: connecting to pole-top at %s:%d", pole_top_host, ROVER_PACKET_PORT);

    /* --- Signal handling ------------------------------------------------ */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    log_info("ui_client: render loop running");

    /* --- Render loop at 2 Hz ------------------------------------------- */
    while (g_running) {
        gm_position_t snap;
        int valid;
        pthread_mutex_lock(&fix_state.mutex);
        snap = fix_state.position;
        valid = fix_state.valid;
        pthread_mutex_unlock(&fix_state.mutex);

        screen_update(&snap, (bool)valid, monotonic_ms());

        usleep(UI_INTERVAL_MS * 1000u);
    }

    log_info("ui_client: shutting down");

    /* --- Cleanup (reverse init order) ---------------------------------- */
    stream_client_stop();
    display_close();
    pthread_mutex_destroy(&fix_state.mutex);

    log_info("ui_client: shutdown complete");
    return GM_OK;
}