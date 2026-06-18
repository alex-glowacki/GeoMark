/**
 * @file ui/screens/sleep_screen.c
 * @brief Sleep screen logic — no ui/tft/display.h dependency.
 */

#define _GNU_SOURCE

#include "ui/screens/sleep_screen.h"

#include <stdbool.h>
#include <string.h>

void sleep_screen_init(SleepScreenCtx *ctx, UiScreenStack *stack, UiScreen wake_target)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack       = stack;
    ctx->wake_target = wake_target;
}

static bool sleep_on_event(void *raw_ctx, UiEvent ev)
{
    SleepScreenCtx *ctx = (SleepScreenCtx *)raw_ctx;

    switch (ev.type) {
    case UI_EVENT_NAV_UP:
    case UI_EVENT_NAV_DOWN:
    case UI_EVENT_NAV_LEFT:
    case UI_EVENT_NAV_RIGHT:
    case UI_EVENT_ACTIVATE:
    case UI_EVENT_TAP:
        /* "Tap anywhere to wake" from geomark-ui-redesign-decisions.md,
         * generalized to any d-pad press while the DSI touch panel isn't
         * installed yet. */
        ui_stack_push(ctx->stack, ctx->wake_target);
        return true;

    case UI_EVENT_BACK:
    case UI_EVENT_NONE:
    default:
        /* BACK means nothing at the root — fall through unconsumed so the
         * screen stack's own depth > 1 guard treats it as a no-op instead
         * of Sleep mistakenly interpreting it as a wake trigger. */
        return false;
    }
}

UiScreen sleep_screen_as_ui_screen(SleepScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_event  = sleep_on_event;
    s.on_render = sleep_screen_render;
    s.ctx       = ctx;
    return s;
}