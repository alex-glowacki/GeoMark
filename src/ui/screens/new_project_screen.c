/**
 * @file ui/screens/new_project_screen.c
 * @brief New Project screen logic. No ui/tft/display.h dependency (other
 *        than through ui/core/keyboard.h's KEYBOARD_TOP_Y/HEIGHT, which
 *        are plain constants, not function calls) -- same split as every
 *        other screen, so this stays unit-testable on host.
 */

#define _GNU_SOURCE

#include "ui/screens/new_project_screen.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/log.h"

/* -------------------------------------------------------------------------
 * Layout
 * ---------------------------------------------------------------------- */

#define FORM_MARGIN    20
#define FORM_LABEL_Y   60
#define FIELD_Y        92
#define FIELD_H        44
#define FIELD_W        500
#define CREATE_BTN_X   540
#define CREATE_BTN_W   240
#define CREATE_BTN_H   44

/* -------------------------------------------------------------------------
 * Project directory creation
 *
 * ~/geomark-data/projects/<name>/ — three levels, any of which may already
 * exist. Single-level mkdir() per the same convention survey/export.c's
 * mkdir_if_missing() already uses; called three times here rather than
 * adding a recursive helper, since the depth is fixed and known (this is
 * not a general-purpose mkdir -p).
 * ---------------------------------------------------------------------- */

static bool mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("new_project: cannot create directory %s: %s",
                 path, strerror(errno));
        return false;
    }
    return true;
}

/**
 * Returns:
 *   0  on success (directory created or already existed)
 *   1  if ~/geomark-data/projects/<name> already existed (not an error --
 *      caller decides whether that's NEW_PROJECT_STATUS_ALREADY_EXISTS)
 *  -1  on any I/O error (HOME unset, or any mkdir failure other than
 *      EEXIST at any level)
 */
static int create_project_dir(const char *name)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        log_error("new_project: HOME is not set -- cannot resolve ~/geomark-data");
        return -1;
    }

    char base[256];
    char projects[280];
    char project[320];

    snprintf(base,     sizeof(base),     "%s/geomark-data", home);
    snprintf(projects, sizeof(projects), "%s/projects", base);
    snprintf(project,  sizeof(project),  "%s/%s", projects, name);

    if (!mkdir_if_missing(base))     return -1;
    if (!mkdir_if_missing(projects)) return -1;

    struct stat st;
    bool already_existed = (stat(project, &st) == 0);

    if (!mkdir_if_missing(project)) return -1;

    return already_existed ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Widget callbacks
 * ---------------------------------------------------------------------- */

static void on_create(UiWidget *self, void *screen_ctx)
{
    (void)self;
    NewProjectScreenCtx *ctx = (NewProjectScreenCtx *)screen_ctx;

    if (ctx->name_len == 0) {
        ctx->status = NEW_PROJECT_STATUS_EMPTY_NAME;
        ctx->name_len_at_status = ctx->name_len;
        return;
    }

    int rc = create_project_dir(ctx->name_buf);
    if (rc < 0) {
        ctx->status = NEW_PROJECT_STATUS_IO_ERROR;
        ctx->name_len_at_status = ctx->name_len;
        return;
    }
    if (rc == 1) {
        ctx->status = NEW_PROJECT_STATUS_ALREADY_EXISTS;
        ctx->name_len_at_status = ctx->name_len;
        return;
    }

    log_info("new_project: created project '%s'", ctx->name_buf);
    ctx->status = NEW_PROJECT_STATUS_NONE;
    ui_stack_push(ctx->stack, ctx->job_setup_screen);
}

static void on_keyboard_done(void *screen_ctx)
{
    /* Done currently just drops keyboard focus back to the name field's
     * own widget rather than submitting -- Create is its own separate
     * button, deliberately not auto-triggered by Done, so a person can
     * keep typing after Done if they want without it firing Create early. */
    NewProjectScreenCtx *ctx = (NewProjectScreenCtx *)screen_ctx;
    for (uint32_t i = 0; i < ctx->grid.count; i++) {
        if (ctx->grid.widgets[i].kind == WIDGET_TEXT_FIELD) {
            if (ctx->grid.focus_idx >= 0 &&
                ctx->grid.focus_idx < (int32_t)ctx->grid.count)
                ctx->grid.widgets[ctx->grid.focus_idx].focused = false;
            ctx->grid.widgets[i].focused = true;
            ctx->grid.focus_idx = (int32_t)i;
            return;
        }
    }
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void new_project_screen_init(NewProjectScreenCtx *ctx, UiScreenStack *stack,
                             UiScreen job_setup_screen)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack             = stack;
    ctx->job_setup_screen  = job_setup_screen;

    /* kb (UiKeyboardTarget) is first in the struct -- see the header's
     * doc comment. Point it at this screen's own name buffer up front;
     * unlike a multi-field screen, New Project has exactly one text
     * field, so there is no "switch active target on focus" step needed
     * here -- kb.buf never needs to repoint after this. */
    ctx->kb.buf        = ctx->name_buf;
    ctx->kb.buf_cap    = sizeof(ctx->name_buf);
    ctx->kb.len         = &ctx->name_len;
    ctx->kb.on_done     = on_keyboard_done;
    ctx->kb.screen_ctx  = ctx;

    ui_grid_init(&ctx->grid, ctx);

    UiRect name_label_r = { FORM_MARGIN, FORM_LABEL_Y, 200, 20 };
    ui_grid_add_label(&ctx->grid, name_label_r, "Project Name");

    UiRect name_field_r = { FORM_MARGIN, FIELD_Y, FIELD_W, FIELD_H };
    ui_grid_add_text_field(&ctx->grid, name_field_r, "Project Name",
                           ctx->name_buf, sizeof(ctx->name_buf));

    UiRect create_r = { CREATE_BTN_X, FIELD_Y, CREATE_BTN_W, CREATE_BTN_H };
    ui_grid_add_button(&ctx->grid, create_r, "Create Project", on_create);

    keyboard_add_to_grid(&ctx->grid, &ctx->kb_labels);
}

static void new_project_on_enter(void *raw_ctx)
{
    NewProjectScreenCtx *ctx = (NewProjectScreenCtx *)raw_ctx;
    /* The name field is the first focusable widget added in init() (the
     * label before it is WIDGET_LABEL, never focusable), so
     * ui_grid_focus_first() already lands here correctly -- including
     * clearing whatever was focused before, which a hand-rolled version
     * of this function would need to duplicate. */
    ui_grid_focus_first(&ctx->grid);
}

static bool new_project_on_event(void *raw_ctx, UiEvent ev)
{
    NewProjectScreenCtx *ctx = (NewProjectScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Main Menu */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen new_project_screen_as_ui_screen(NewProjectScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = new_project_on_enter;
    s.on_event  = new_project_on_event;
    s.on_render = new_project_screen_render;
    s.ctx       = ctx;
    return s;
}