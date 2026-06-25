/**
 * @file ui/screens/continue_project_screen.c
 * @brief Continue Existing Project screen logic. No ui/tft/display.h
 *        dependency (other than TFT_WIDTH, a plain constant, same
 *        convention as open_job_screen.c) -- stays unit-testable on host.
 */

#define _GNU_SOURCE

#include "ui/screens/continue_project_screen.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ui/tft/display.h" /* TFT_WIDTH only */
#include "util/log.h"

/* -------------------------------------------------------------------------
 * Layout -- identical constants to open_job_screen.c's list screen; same
 * title height, same scroll region, same row sizing. Kept as its own
 * #define block rather than sharing open_job_screen.c's (private, static)
 * ones, since the two screens have no header in common to put shared
 * layout constants in and don't need one for four numbers.
 * ---------------------------------------------------------------------- */

#define LIST_MARGIN     20
#define LIST_TOP_Y      90
#define LIST_BOTTOM_Y   460
#define LIST_ROW_H      48
#define LIST_ROW_GAP    10
#define LIST_BTN_W      (TFT_WIDTH - 2 * LIST_MARGIN)

/* How many project rows fit in the visible list region without scrolling
 * -- same derivation and rationale as open_job_screen.c's
 * LIST_VISIBLE_ROWS (kept as its own #define for the same reason the
 * layout block above is its own copy rather than shared). */
#define LIST_VISIBLE_ROWS (((LIST_BOTTOM_Y - LIST_TOP_Y) + LIST_ROW_GAP) / (LIST_ROW_H + LIST_ROW_GAP))

/* Up/Down nav buttons -- top-right corner, same placement and rationale
 * as open_job_screen.c's own NAV_BUTTON_X/NAV_UP_Y/NAV_DOWN_Y (verified
 * clear of this screen's title, "Continue Existing Project" -- the
 * longest title in the codebase, ends at x=550, see widget.h's back
 * button doc comment -- and of the back button at x=8..78). Only added
 * when ctx->project_count exceeds LIST_VISIBLE_ROWS -- see
 * rebuild_project_list(). */
#define NAV_BUTTON_X      (TFT_WIDTH - 8 - UI_NAV_BUTTON_W)
#define NAV_UP_Y          4
#define NAV_DOWN_Y        (NAV_UP_Y + UI_NAV_BUTTON_H + 4)

/* -------------------------------------------------------------------------
 * Directory scan
 *
 * Resolves ~/geomark-data/projects/ and lists every subdirectory as a
 * candidate project -- same "a project's existence is its directory"
 * convention new_project_screen.c's create_project_dir() establishes.
 * "." and ".." are skipped; non-directory entries are skipped too.
 * ---------------------------------------------------------------------- */

static void scan_projects(ContinueProjectScreenCtx *ctx)
{
    ctx->project_count = 0;
    ctx->status        = CONTINUE_PROJECT_STATUS_NONE;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        log_error("continue_project: HOME is not set -- cannot resolve ~/geomark-data");
        ctx->status = CONTINUE_PROJECT_STATUS_NO_PROJECTS;
        return;
    }

    char projects_dir[400];
    snprintf(projects_dir, sizeof(projects_dir), "%s/geomark-data/projects", home);

    DIR *d = opendir(projects_dir);
    if (!d) {
        log_warn("continue_project: cannot open projects directory '%s'", projects_dir);
        ctx->status = CONTINUE_PROJECT_STATUS_NO_PROJECTS;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (ctx->project_count >= CONTINUE_PROJECT_MAX_LISTED)
            break; /* CONTINUE_PROJECT_MAX_LISTED is a generous ceiling, see header */

        char entry_path[900];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", projects_dir, entry->d_name);

        struct stat st;
        if (stat(entry_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        snprintf(ctx->project_names[ctx->project_count], PROJECT_CONTEXT_NAME_MAX,
                 "%.*s", PROJECT_CONTEXT_NAME_MAX - 1, entry->d_name);
        ctx->project_count++;
    }
    closedir(d);

    if (ctx->project_count == 0)
        ctx->status = CONTINUE_PROJECT_STATUS_NO_PROJECTS;
}

/* -------------------------------------------------------------------------
 * Widget callback -- fires once per project button. Writes the selected
 * project's name into the shared ProjectContext and pushes Job Setup --
 * the same destination and ProjectContext-write New Project's on_create()
 * uses on a successful Create, except the directory already exists here
 * instead of being created.
 * ---------------------------------------------------------------------- */

static void on_project_selected(UiWidget *self, void *screen_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)screen_ctx;
    const char *project_name = self->label; /* points into ctx->project_names[i] */

    if (ctx->project_ctx)
        project_context_set(ctx->project_ctx, project_name);

    log_info("continue_project: selected project '%s'", project_name);
    ui_stack_push(ctx->stack, ctx->job_setup_screen);
}

/** See ui/core/widget.h's ui_grid_add_back_button() doc comment. */
static void on_back(UiWidget *self, void *screen_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

/** See ui/core/widget.h's ui_grid_add_nav_up_button()/_down_button() doc
 * comment, and open_job_screen.c's own on_nav_up()/on_nav_down() for the
 * identical rationale (only added when the list actually overflows, and
 * restoring scroll_anchor_idx before moving focus for the same reason
 * documented in continue_project_screen.h). */
static void on_nav_up(UiWidget *self, void *screen_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)screen_ctx;
    (void)self;
    if (ctx->scroll_anchor_idx >= 0 && ctx->scroll_anchor_idx < (int32_t)ctx->grid.count)
        ctx->grid.focus_idx = ctx->scroll_anchor_idx;
    ui_grid_move_focus(&ctx->grid, UI_EVENT_NAV_UP);
    ctx->scroll_anchor_idx = ctx->grid.focus_idx;
}

static void on_nav_down(UiWidget *self, void *screen_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)screen_ctx;
    (void)self;
    if (ctx->scroll_anchor_idx >= 0 && ctx->scroll_anchor_idx < (int32_t)ctx->grid.count)
        ctx->grid.focus_idx = ctx->scroll_anchor_idx;
    ui_grid_move_focus(&ctx->grid, UI_EVENT_NAV_DOWN);
    ctx->scroll_anchor_idx = ctx->grid.focus_idx;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void continue_project_screen_init(ContinueProjectScreenCtx *ctx, UiScreenStack *stack,
                                  UiScreen job_setup_screen, ProjectContext *project_ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack            = stack;
    ctx->job_setup_screen = job_setup_screen;
    ctx->project_ctx      = project_ctx;

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, LIST_TOP_Y, TFT_WIDTH, (uint16_t)(LIST_BOTTOM_Y - LIST_TOP_Y)});
}

/**
 * Rebuilds the grid's project-button list from a fresh directory scan.
 * Called from on_enter (not init) -- same "reflect current on-disk
 * state every time this screen becomes visible" rationale as
 * open_job_screen.c's rebuild_job_list(): a project created in some
 * other process run (or even just earlier in this one, before this
 * screen's ctx existed) must show up without restarting anything.
 */
static void rebuild_project_list(ContinueProjectScreenCtx *ctx)
{
    scan_projects(ctx);

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, LIST_TOP_Y, TFT_WIDTH, (uint16_t)(LIST_BOTTOM_Y - LIST_TOP_Y)});

    uint16_t y = LIST_TOP_Y + 4;
    for (size_t i = 0; i < ctx->project_count; i++) {
        UiRect r = { LIST_MARGIN, y, LIST_BTN_W, LIST_ROW_H };
        UiWidget *w = ui_grid_add_button(&ctx->grid, r, ctx->project_names[i],
                                         on_project_selected);
        ui_widget_mark_scrollable(w);
        y = (uint16_t)(y + LIST_ROW_H + LIST_ROW_GAP);
    }

    /* Added last, after the project list, so ui_grid_focus_first() in
     * continue_project_on_enter() below still lands on the first project
     * button when the list is non-empty -- same focus-order reasoning as
     * open_job_screen.c's rebuild_job_list(). If the list is empty, this
     * is the only focusable widget, so focus correctly lands here instead. */
    ui_grid_add_back_button(&ctx->grid, on_back);

    /* Only when the list actually overflows the visible region -- same
     * rationale as open_job_screen.c's rebuild_job_list(). */
    if (ctx->project_count > LIST_VISIBLE_ROWS) {
        ui_grid_add_nav_up_button(&ctx->grid,
            (UiRect){NAV_BUTTON_X, NAV_UP_Y, UI_NAV_BUTTON_W, UI_NAV_BUTTON_H}, on_nav_up);
        ui_grid_add_nav_down_button(&ctx->grid,
            (UiRect){NAV_BUTTON_X, NAV_DOWN_Y, UI_NAV_BUTTON_W, UI_NAV_BUTTON_H}, on_nav_down);
    }
}

static void continue_project_on_enter(void *raw_ctx)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)raw_ctx;
    rebuild_project_list(ctx);
    ui_grid_focus_first(&ctx->grid); /* no-op (returns false) if project_count == 0 */
}

static bool continue_project_on_event(void *raw_ctx, UiEvent ev)
{
    ContinueProjectScreenCtx *ctx = (ContinueProjectScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Main Menu */

    /* Capture focus_idx before this event reaches the grid -- see
     * scroll_anchor_idx's doc comment in continue_project_screen.h and
     * open_job_screen.c's identical on_event for the full rationale. */
    ctx->scroll_anchor_idx = ctx->grid.focus_idx;

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen continue_project_screen_as_ui_screen(ContinueProjectScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = continue_project_on_enter;
    s.on_event  = continue_project_on_event;
    s.on_render = continue_project_screen_render;
    s.ctx       = ctx;
    return s;
}