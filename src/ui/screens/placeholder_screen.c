/**
 * @file ui/screens/placeholder_screen.c
 * @brief Placeholder screen logic — no ui/tft/display.h dependency.
 */

#define _GNU_SOURCE

#include "ui/screens/placeholder_screen.h"

#include <stdbool.h>
#include <string.h>

void placeholder_screen_init(PlaceholderScreenCtx *ctx, const char *message)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->message = message;
}

static bool placeholder_on_event(void *raw_ctx, UiEvent ev)
{
    (void)raw_ctx;
    (void)ev;
    return false;
}

UiScreen placeholder_screen_as_ui_screen(PlaceholderScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_event  = placeholder_on_event;
    s.on_render = placeholder_screen_render;
    s.ctx       = ctx;
    return s;
}