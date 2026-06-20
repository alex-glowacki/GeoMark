/**
 * @file ui/client.c
 * @brief UI client mode implementation for the Pi 5 handheld.
 *
 * Wires together:
 *   stream_client  →  packet callback  →  UISharedState (mutex-protected)
 *   gpio_button_poll() → InputEvent → survey_screen_input()
 *   2 Hz loop      →  screen_update() OR survey_screen_tick()
 *                  →  survey_screen_feed() when capture active
 *
 * UI modes:
 *   CLIENT_MODE_STATUS  — RTK status screen (default)
 *   CLIENT_MODE_SURVEY  — Survey session active, survey_screen drives TFT
 *
 * Drives the Hosyond 7" DSI panel via the framebuffer backend
 * (ui/tft/display.c). Capacitive touch input is wired into the new
 * --ui-preview widget UI (ui/preview.c) only -- this legacy survey-capture
 * path has no widget grid to hit-test taps against, so it stays
 * button-only for now (see ui/gpio_button.h).
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "geomark.h"
#include "net/rover_packet.h"
#include "net/stream_client.h"
#include "survey/codelist.h"
#include "survey/export.h"
#include "survey/survey.h"
#include "ui/client.h"
#include "ui/gpio_button.h"
#include "ui/input.h"
#include "ui/survey_screen.h"
#include "ui/tft/display.h"
#include "ui/tft/screen.h"
#include "util/log.h"

/* --------------------------------------------------------------------------
 * Hardware config — Pi 5, Hosyond 7" DSI panel via framebuffer
 * -------------------------------------------------------------------------- */

/* 2 Hz render loop */
#define UI_INTERVAL_MS  500u

/* SCP transfer timeout */
#define SCP_TIMEOUT_S   30

/* --------------------------------------------------------------------------
 * Client display mode
 * -------------------------------------------------------------------------- */

typedef enum {
    CLIENT_MODE_STATUS = 0,  /* RTK status screen  */
    CLIENT_MODE_SURVEY,      /* Survey session active */
} ClientMode;

/* --------------------------------------------------------------------------
 * Shared state — written by packet callback thread, read by main loop
 * -------------------------------------------------------------------------- */

typedef struct {
    gm_position_t   position;
    pthread_mutex_t mutex;
    int             valid;
} UISharedState;

/* --------------------------------------------------------------------------
 * Signal handling
 * -------------------------------------------------------------------------- */

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* --------------------------------------------------------------------------
 * Packet callback — runs on stream_client receive thread
 * -------------------------------------------------------------------------- */

static void packet_callback(const RoverStatusPacket *pkt, void *user)
{
    UISharedState *state = (UISharedState *)user;

    if (!pkt->valid)
        return;

    gm_position_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.latitude     = pkt->lat;
    pos.longitude    = pkt->lon;
    pos.altitude     = pkt->alt_msl;
    pos.hdop         = (float)pkt->hdop;
    pos.satellites   = pkt->num_sats;
    pos.fix_type     = (gm_fix_type_t)pkt->fix_quality;
    pos.timestamp_ms = 0;

    pthread_mutex_lock(&state->mutex);
    state->position = pos;
    state->valid    = 1;
    pthread_mutex_unlock(&state->mutex);
}

/* --------------------------------------------------------------------------
 * Monotonic clock helper
 * -------------------------------------------------------------------------- */

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* --------------------------------------------------------------------------
 * Session helpers
 * -------------------------------------------------------------------------- */

static void make_session_name(char *buf, size_t len)
{
    time_t     now = time(NULL);
    struct tm *utc = gmtime(&now);
    strftime(buf, len, "%Y%m%d_%H%M%S", utc);
}

static int start_session(SurveySession *session, char *csv_path_out,
                         size_t csv_path_len)
{
    char dir[256];
    export_resolve_output_dir(dir, sizeof(dir));

    char name[32];
    make_session_name(name, sizeof(name));

    snprintf(csv_path_out, csv_path_len, "%s/%s.csv", dir, name);

    if (survey_session_open(session, csv_path_out, name) != 0) {
        log_error("ui_client: failed to open session at %s", csv_path_out);
        return -1;
    }

    log_info("ui_client: survey session started — %s", csv_path_out);
    return 0;
}

static void end_session(SurveySession *session, const char *csv_path)
{
    uint32_t n = session->point_count;
    survey_session_close(session);
    log_info("ui_client: session closed — %u point(s)", n);

    if (n == 0) {
        log_info("ui_client: no points collected — skipping SCP");
        return;
    }

    log_info("ui_client: transferring CSV via SCP...");
    int rc = export_scp(csv_path, SCP_TIMEOUT_S);
    if (rc == 0)
        log_info("ui_client: SCP transfer complete");
    else if (rc == 1)
        log_warn("ui_client: SCP transfer failed (scp exited non-zero)");
    else if (rc == 2)
        log_warn("ui_client: SCP transfer timed out");
    else
        log_warn("ui_client: SCP transfer error (fork failed)");
}

/* --------------------------------------------------------------------------
 * Public entry point
 * -------------------------------------------------------------------------- */

gm_status_t ui_client_run(const char *pole_top_host)
{
    /* --- TFT display ---------------------------------------------------- */
    gm_status_t ds = display_open();
    if (ds != GM_OK) {
        log_error("ui_client: display_open failed");
        return GM_ERR_IO;
    }
    screen_init();
    log_info("ui_client: TFT display ready");

    /* --- GPIO buttons --------------------------------------------------- */
    gm_status_t bs = gpio_button_open();
    if (bs != GM_OK) {
        log_error("ui_client: gpio_button_open failed");
        display_close();
        return GM_ERR_IO;
    }
    log_info("ui_client: GPIO buttons ready");

    /* --- Shared fix state ----------------------------------------------- */
    UISharedState shared;
    memset(&shared, 0, sizeof(shared));
    pthread_mutex_init(&shared.mutex, NULL);

    /* --- Stream client -------------------------------------------------- */
    gm_status_t cs = stream_client_start(pole_top_host,
                                          packet_callback, &shared);
    if (cs != GM_OK) {
        log_error("ui_client: stream_client_start failed");
        gpio_button_close();
        display_close();
        pthread_mutex_destroy(&shared.mutex);
        return GM_ERR_IO;
    }
    log_info("ui_client: connecting to %s:%d", pole_top_host, ROVER_PACKET_PORT);

    /* --- Signal handling ------------------------------------------------ */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* --- Code list ------------------------------------------------------ */
    CodeList codelist;
    codelist_load(&codelist);

    /* --- Survey state --------------------------------------------------- */
    SurveyScreenCtx survey_ctx;
    SurveySession   session;
    char            csv_path[320];
    memset(&session,  0, sizeof(session));
    memset(csv_path,  0, sizeof(csv_path));

    ClientMode mode = CLIENT_MODE_STATUS;

    log_info("ui_client: render loop running");

    /* --- Main loop at 2 Hz --------------------------------------------- */
    while (g_running) {
        uint32_t now = monotonic_ms();

        /* Snapshot latest fix under lock */
        gm_position_t snap;
        int           valid;
        pthread_mutex_lock(&shared.mutex);
        snap  = shared.position;
        valid = shared.valid;
        pthread_mutex_unlock(&shared.mutex);

        /* ---------------------------------------------------------------- */
        /* Button input                                                      */
        /* ---------------------------------------------------------------- */
        InputEvent ev = gpio_button_poll();

        if (ev != INPUT_NONE) {
            if (mode == CLIENT_MODE_STATUS) {
                /* Status screen: Center starts a survey session */
                if (ev == INPUT_BTN_CENTER) {
                    log_info("ui_client: center button — starting survey");
                    if (start_session(&session, csv_path,
                                      sizeof(csv_path)) == 0) {
                        survey_screen_init(&survey_ctx, &codelist);
                        mode = CLIENT_MODE_SURVEY;
                    }
                }
            } else {
                /* Survey mode: forward event to survey screen */
                survey_screen_input(&survey_ctx, ev, &session);

                /* Detect session end: state returns to IDLE with open session */
                if (survey_ctx.state == SURVEY_UI_IDLE && session.open) {
                    end_session(&session, csv_path);
                    memset(&session, 0, sizeof(session));
                    mode = CLIENT_MODE_STATUS;
                    screen_init();
                    screen_update(&snap, (bool)valid, now);
                }
            }
        }

        /* ---------------------------------------------------------------- */
        /* GNSS feed — only during active capture                           */
        /* ---------------------------------------------------------------- */
        if (mode == CLIENT_MODE_SURVEY &&
            survey_ctx.state == SURVEY_UI_CAPTURE && valid) {

            survey_screen_feed(&survey_ctx, &session,
                               snap.latitude,
                               snap.longitude,
                               snap.altitude,
                               (uint8_t)snap.fix_type,
                               (double)snap.hdop,
                               snap.satellites);
        }

        /* ---------------------------------------------------------------- */
        /* Render                                                            */
        /* ---------------------------------------------------------------- */
        if (mode == CLIENT_MODE_STATUS) {
            screen_update(&snap, (bool)valid, now);
        } else {
            survey_screen_tick(&survey_ctx, now);
        }
        display_present();

        usleep(UI_INTERVAL_MS * 1000u);
    }

    log_info("ui_client: shutting down");

    /* --- End any open session on SIGINT --------------------------------- */
    if (mode == CLIENT_MODE_SURVEY && session.open)
        end_session(&session, csv_path);

    /* --- Cleanup (reverse init order) ---------------------------------- */
    stream_client_stop();
    gpio_button_close();
    display_close();
    pthread_mutex_destroy(&shared.mutex);

    log_info("ui_client: shutdown complete");
    return GM_OK;
}