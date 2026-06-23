/**
 * @file ui/screens/placeholder_screen.c
 * @brief Placeholder screen logic — no ui/tft/display.h dependency.
 */

#define _GNU_SOURCE

#include "ui/screens/placeholder_screen.h"

#include <stdbool.h>
#include <string.h>

/** See ui/core/widget.h's ui_grid_add_back_button() doc comment. */
static void on_back(UiWidget *self, void *screen_ctx)
{
    PlaceholderScreenCtx *ctx = (PlaceholderScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

void placeholder_screen_init(PlaceholderScreenCtx *ctx, UiScreenStack *stack,
                             const char *message)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack   = stack;
    ctx->message = message;

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_add_back_button(&ctx->grid, on_back);
}

static void placeholder_on_enter(void *raw_ctx)
{
    PlaceholderScreenCtx *ctx = (PlaceholderScreenCtx *)raw_ctx;
    ui_grid_focus_first(&ctx->grid);
}

static bool placeholder_on_event(void *raw_ctx, UiEvent ev)
{
    PlaceholderScreenCtx *ctx = (PlaceholderScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to whatever pushed this */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen placeholder_screen_as_ui_screen(PlaceholderScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = placeholder_on_enter;
    s.on_event  = placeholder_on_event;
    s.on_render = placeholder_screen_render;
    s.ctx       = ctx;
    return s;
}