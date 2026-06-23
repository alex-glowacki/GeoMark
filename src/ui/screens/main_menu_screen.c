/**
 * @file ui/screens/main_menu_screen.c
 * @brief Main menu logic. Includes ui/tft/display.h only for the
 *        TFT_WIDTH layout constant (preprocessor macro, no function call,
 *        so this file still links and tests without display.c).
 */

#define _GNU_SOURCE

#include "ui/screens/main_menu_screen.h"

#include <stdbool.h>
#include <string.h>

#include "ui/tft/display.h" /* TFT_WIDTH only */

#define MENU_MARGIN 20
#define MENU_BTN_H  56
#define MENU_GAP    18
#define MENU_BTN_W  (TFT_WIDTH - 2 * MENU_MARGIN)
#define MENU_TOP_Y  90

static void on_new_project(UiWidget *self, void *screen_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_push(ctx->stack, ctx->new_project_screen);
}

static void on_continue_project(UiWidget *self, void *screen_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_push(ctx->stack, ctx->continue_project_screen);
}

static void on_stats(UiWidget *self, void *screen_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_push(ctx->stack, ctx->stats_screen);
}

/**
 * Touch-only replacement for the physical Left/BACK button (see
 * ui/core/widget.h's ui_grid_add_back_button() doc comment) -- dispatches
 * a UI_EVENT_BACK through the stack rather than popping directly, so this
 * screen's own on_event() BACK handling below runs exactly as it would
 * for a real BACK event from any other input source.
 */
static void on_back(UiWidget *self, void *screen_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

void main_menu_screen_init(MainMenuScreenCtx *ctx, UiScreenStack *stack,
                           UiScreen new_project_screen,
                           UiScreen continue_project_screen,
                           UiScreen stats_screen)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack                   = stack;
    ctx->new_project_screen      = new_project_screen;
    ctx->continue_project_screen = continue_project_screen;
    ctx->stats_screen            = stats_screen;

    ui_grid_init(&ctx->grid, ctx);

    UiRect r1 = { MENU_MARGIN, MENU_TOP_Y,                                MENU_BTN_W, MENU_BTN_H };
    UiRect r2 = { MENU_MARGIN, MENU_TOP_Y + (MENU_BTN_H + MENU_GAP),      MENU_BTN_W, MENU_BTN_H };
    UiRect r3 = { MENU_MARGIN, MENU_TOP_Y + 2 * (MENU_BTN_H + MENU_GAP),  MENU_BTN_W, MENU_BTN_H };

    ui_grid_add_button(&ctx->grid, r1, "Start New Project",         on_new_project);
    ui_grid_add_button(&ctx->grid, r2, "Continue Existing Project", on_continue_project);
    ui_grid_add_button(&ctx->grid, r3, "View GeoMark Stats",        on_stats);
    ui_grid_add_back_button(&ctx->grid, on_back);
}

static void main_menu_on_enter(void *raw_ctx)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)raw_ctx;
    ui_grid_focus_first(&ctx->grid);
}

static bool main_menu_on_event(void *raw_ctx, UiEvent ev)
{
    MainMenuScreenCtx *ctx = (MainMenuScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed — stack pops back to Sleep by default policy */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen main_menu_screen_as_ui_screen(MainMenuScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = main_menu_on_enter;
    s.on_event  = main_menu_on_event;
    s.on_render = main_menu_screen_render;
    s.ctx       = ctx;
    return s;
}