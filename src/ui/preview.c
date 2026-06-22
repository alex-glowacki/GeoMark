/**
 * @file ui/preview.c
 * @brief See ui/preview.h.
 */

#define _GNU_SOURCE

#include "ui/preview.h"

#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "net/rtk_feed_client.h"
#include "ui/core/screen_stack.h"
#include "ui/core/touch_input.h"
#include "ui/gpio_button.h"
#include "ui/input.h"
#include "ui/screens/continue_project_screen.h"
#include "ui/screens/job_context.h"
#include "ui/screens/job_create_screen.h"
#include "ui/screens/job_setup_screen.h"
#include "ui/screens/main_menu_screen.h"
#include "ui/screens/measure_points_screen.h"
#include "ui/screens/new_project_screen.h"
#include "ui/screens/open_job_screen.h"
#include "ui/screens/placeholder_screen.h"
#include "ui/screens/project_context.h"
#include "ui/screens/sleep_screen.h"
#include "ui/tft/display.h"
#include "util/log.h"

#define PREVIEW_INTERVAL_MS 100u /* 10 Hz -- more responsive than the 2 Hz
                                  * production loop since there's no GNSS
                                  * feed competing for the CPU here */

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/**
 * Bridge the legacy button-only InputEvent to the new UiEvent. Left maps
 * to BACK rather than NAV_LEFT -- see ui/preview.h controls doc. This is
 * the single translation point ui/core/ui_event.h's header comment refers
 * to. The evdev touch driver (ui/core/touch_input.c) does NOT go through
 * this bridge -- it produces UiEvent{UI_EVENT_TAP, x, y} directly, exactly
 * as that same header comment anticipated.
 */
static UiEvent translate_input(InputEvent ev)
{
    UiEvent out = { .type = UI_EVENT_NONE };

    switch (ev) {
    case INPUT_BTN_UP:     out.type = UI_EVENT_NAV_UP;    break;
    case INPUT_BTN_DOWN:   out.type = UI_EVENT_NAV_DOWN;  break;
    case INPUT_BTN_LEFT:   out.type = UI_EVENT_BACK;      break;
    case INPUT_BTN_RIGHT:  out.type = UI_EVENT_NAV_RIGHT; break;
    case INPUT_BTN_CENTER: out.type = UI_EVENT_ACTIVATE;  break;
    case INPUT_NONE:
    default:               out.type = UI_EVENT_NONE;      break;
    }
    return out;
}

gm_status_t ui_preview_run(const char *pole_top_host)
{
    gm_status_t ds = display_open();
    if (ds != GM_OK) {
        log_error("ui_preview: display_open failed");
        return GM_ERR_IO;
    }

    gm_status_t bs = gpio_button_open();
    if (bs != GM_OK) {
        log_error("ui_preview: gpio_button_open failed");
        display_close();
        return GM_ERR_IO;
    }

    /* Touch is optional -- preview still runs button-only if no
     * capacitive touch device is found (e.g. testing on a panel that
     * isn't connected yet, or before the evdev node is enumerated). */
    gm_status_t ts = touch_input_open();
    bool touch_available = (ts == GM_OK);
    if (!touch_available)
        log_warn("ui_preview: no touch device found -- running button-only");

    /* --- RTK feed (Measure Points' live fix) ----------------------------- */
    RtkFeedClient feed_client;
    gm_status_t fs = rtk_feed_client_start(&feed_client, pole_top_host);
    if (fs != GM_OK) {
        log_error("ui_preview: rtk_feed_client_start failed");
        if (touch_available)
            touch_input_close();
        gpio_button_close();
        display_close();
        return GM_ERR_IO;
    }
    log_info("ui_preview: connecting to rover at %s", pole_top_host);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    UiScreenStack stack;
    ui_stack_init(&stack);

    ProjectContext project_ctx;
    project_context_init(&project_ctx);

    JobContext job_ctx;
    job_context_init(&job_ctx);

    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&stats_stub, "Stats -- not built yet");

    MeasurePointsScreenCtx measure_points_ctx;
    measure_points_screen_init(&measure_points_ctx, &stack, &job_ctx,
                               rtk_feed_client_as_feed(&feed_client));

    JobCreateScreenCtx job_create_ctx;
    job_create_screen_init(&job_create_ctx, &stack,
                           measure_points_screen_as_ui_screen(&measure_points_ctx),
                           &project_ctx, &job_ctx);

    OpenJobScreenCtx open_job_ctx;
    open_job_screen_init(&open_job_ctx, &stack,
                         measure_points_screen_as_ui_screen(&measure_points_ctx),
                         &project_ctx, &job_ctx);

    JobSetupScreenCtx job_setup_ctx;
    job_setup_screen_init(&job_setup_ctx, &stack,
                          job_create_screen_as_ui_screen(&job_create_ctx),
                          open_job_screen_as_ui_screen(&open_job_ctx));

    ContinueProjectScreenCtx continue_project_ctx;
    continue_project_screen_init(&continue_project_ctx, &stack,
                                 job_setup_screen_as_ui_screen(&job_setup_ctx),
                                 &project_ctx);

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            job_setup_screen_as_ui_screen(&job_setup_ctx),
                            &project_ctx);

    MainMenuScreenCtx menu_ctx;
    main_menu_screen_init(&menu_ctx, &stack,
                          new_project_screen_as_ui_screen(&new_project_ctx),
                          continue_project_screen_as_ui_screen(&continue_project_ctx),
                          placeholder_screen_as_ui_screen(&stats_stub));

    SleepScreenCtx sleep_ctx;
    sleep_screen_init(&sleep_ctx, &stack, main_menu_screen_as_ui_screen(&menu_ctx));

    ui_stack_push(&stack, sleep_screen_as_ui_screen(&sleep_ctx));

    log_info("ui_preview: running (Up/Down move, Center activate, Left back, "
             "tap%s supported, Ctrl+C exit)",
             touch_available ? "" : " NOT");

    while (g_running) {
        InputEvent legacy_ev = gpio_button_poll();
        UiEvent    btn_ev    = translate_input(legacy_ev);

        if (btn_ev.type != UI_EVENT_NONE)
            ui_stack_dispatch_event(&stack, btn_ev);

        if (touch_available) {
            UiEvent tap_ev;
            if (touch_input_poll(&tap_ev))
                ui_stack_dispatch_event(&stack, tap_ev);
        }

        ui_stack_tick(&stack, monotonic_ms());
        ui_stack_render(&stack);
        display_present();

        usleep(PREVIEW_INTERVAL_MS * 1000u);
    }

    log_info("ui_preview: shutting down");
    rtk_feed_client_stop(&feed_client);
    if (touch_available)
        touch_input_close();
    gpio_button_close();
    display_close();
    return GM_OK;
}