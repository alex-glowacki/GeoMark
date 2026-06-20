/**
 * @file ui/screens/job_setup_screen.c
 * @brief Job Setup logic. Includes ui/tft/display.h only for the
 *        TFT_WIDTH layout constant (preprocessor macro, no function
 *        call), same convention main_menu_screen.c already uses.
 */

#define _GNU_SOURCE

#include "ui/screens/job_setup_screen.h"

#include <stdbool.h>
#include <string.h>

#include "ui/tft/display.h" /* TFT_WIDTH only */

#define JOB_SETUP_MARGIN 20
#define JOB_SETUP_BTN_H  56
#define JOB_SETUP_GAP    18
#define JOB_SETUP_BTN_W  (TFT_WIDTH - 2 * JOB_SETUP_MARGIN)
#define JOB_SETUP_TOP_Y  90

static void on_create_job(UiWidget *self, void *screen_ctx)
{
    JobSetupScreenCtx *ctx = (JobSetupScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_push(ctx->stack, ctx->create_job_screen);
}

static void on_open_job(UiWidget *self, void *screen_ctx)
{
    JobSetupScreenCtx *ctx = (JobSetupScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_push(ctx->stack, ctx->open_job_screen);
}

void job_setup_screen_init(JobSetupScreenCtx *ctx, UiScreenStack *stack,
                           UiScreen create_job_screen, UiScreen open_job_screen)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack             = stack;
    ctx->create_job_screen = create_job_screen;
    ctx->open_job_screen   = open_job_screen;

    ui_grid_init(&ctx->grid, ctx);

    UiRect r1 = { JOB_SETUP_MARGIN, JOB_SETUP_TOP_Y,                             JOB_SETUP_BTN_W, JOB_SETUP_BTN_H };
    UiRect r2 = { JOB_SETUP_MARGIN, JOB_SETUP_TOP_Y + (JOB_SETUP_BTN_H + JOB_SETUP_GAP), JOB_SETUP_BTN_W, JOB_SETUP_BTN_H };

    ui_grid_add_button(&ctx->grid, r1, "Create New Job",   on_create_job);
    ui_grid_add_button(&ctx->grid, r2, "Open Existing Job", on_open_job);
}

static void job_setup_on_enter(void *raw_ctx)
{
    JobSetupScreenCtx *ctx = (JobSetupScreenCtx *)raw_ctx;
    ui_grid_focus_first(&ctx->grid);
}

static bool job_setup_on_event(void *raw_ctx, UiEvent ev)
{
    JobSetupScreenCtx *ctx = (JobSetupScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to New Project */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen job_setup_screen_as_ui_screen(JobSetupScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = job_setup_on_enter;
    s.on_event  = job_setup_on_event;
    s.on_render = job_setup_screen_render;
    s.ctx       = ctx;
    return s;
}