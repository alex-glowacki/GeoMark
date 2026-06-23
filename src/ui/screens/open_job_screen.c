/**
 * @file ui/screens/open_job_screen.c
 * @brief Open Existing Job screen logic. No ui/tft/display.h dependency
 *        (other than TFT_WIDTH, a plain constant, same convention as
 *        job_setup_screen.c) -- stays unit-testable on host.
 */

#define _GNU_SOURCE

#include "ui/screens/open_job_screen.h"

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
 * Layout -- title at the top (matches job_setup_screen.c's JOB_SETUP_TOP_Y
 * convention), scrollable list filling the rest of the panel. No keyboard
 * on this screen, so there's no KEYBOARD_TOP_Y ceiling to respect; the
 * list can use the full height down to the bottom margin.
 * ---------------------------------------------------------------------- */

#define LIST_MARGIN     20
#define LIST_TOP_Y      90
#define LIST_BOTTOM_Y   460
#define LIST_ROW_H      48
#define LIST_ROW_GAP    10
#define LIST_BTN_W      (TFT_WIDTH - 2 * LIST_MARGIN)

/* -------------------------------------------------------------------------
 * Directory scan
 *
 * Resolves ~/geomark-data/projects/<project>/ and lists every
 * subdirectory as a candidate job (a job's own existence is its
 * directory, same convention job_create_screen.c's create_job_dir()
 * already establishes -- job.ini living inside it is incidental to a
 * directory listing, not separately checked here). "." and ".." are
 * skipped; non-directory entries (stray files someone dropped in the
 * project folder) are skipped too, since job.ini itself lives one level
 * down inside each job's own directory, not loose in the project root.
 * ---------------------------------------------------------------------- */

static void scan_jobs(OpenJobScreenCtx *ctx)
{
    ctx->job_count = 0;
    ctx->status    = OPEN_JOB_STATUS_NONE;

    if (!ctx->project_ctx || ctx->project_ctx->name[0] == '\0') {
        ctx->status = OPEN_JOB_STATUS_NO_PROJECT;
        return;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        log_error("open_job: HOME is not set -- cannot resolve ~/geomark-data");
        ctx->status = OPEN_JOB_STATUS_NO_JOBS;
        return;
    }

    char project_dir[400];
    snprintf(project_dir, sizeof(project_dir), "%s/geomark-data/projects/%s",
             home, ctx->project_ctx->name);

    DIR *d = opendir(project_dir);
    if (!d) {
        log_warn("open_job: cannot open project directory '%s'", project_dir);
        ctx->status = OPEN_JOB_STATUS_NO_JOBS;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (ctx->job_count >= OPEN_JOB_MAX_LISTED)
            break; /* OPEN_JOB_MAX_LISTED is a generous ceiling, see open_job_screen.h */

        char entry_path[900];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", project_dir, entry->d_name);

        struct stat st;
        if (stat(entry_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        strncpy(ctx->job_names[ctx->job_count], entry->d_name, GM_JOB_NAME_MAX - 1);
        ctx->job_names[ctx->job_count][GM_JOB_NAME_MAX - 1] = '\0';
        ctx->job_count++;
    }
    closedir(d);

    if (ctx->job_count == 0)
        ctx->status = OPEN_JOB_STATUS_NO_JOBS;
}

/* -------------------------------------------------------------------------
 * Widget callback -- fires once per job button, looks up job.ini for the
 * job whose button was activated, loads it, and pushes Measure Points on
 * success.
 * ---------------------------------------------------------------------- */

static void on_job_selected(UiWidget *self, void *screen_ctx)
{
    OpenJobScreenCtx *ctx = (OpenJobScreenCtx *)screen_ctx;
    const char *job_name  = self->label; /* points into ctx->job_names[i], see init() below */

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0' || !ctx->project_ctx) {
        ctx->status = OPEN_JOB_STATUS_LOAD_ERROR;
        return;
    }

    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/geomark-data/projects/%s/%s/job.ini",
             home, ctx->project_ctx->name, job_name);

    gm_status_t rc = job_metadata_load(ini_path, &ctx->loaded_meta);
    if (rc != GM_OK) {
        log_error("open_job: failed to load '%s'", ini_path);
        ctx->status = OPEN_JOB_STATUS_LOAD_ERROR;
        return;
    }

    log_info("open_job: opened job '%s'", job_name);

    if (ctx->job_ctx)
        job_context_set(ctx->job_ctx, home, ctx->project_ctx->name, job_name);

    ctx->status = OPEN_JOB_STATUS_NONE;
    ui_stack_push(ctx->stack, ctx->measure_points_screen);
}

/** See ui/core/widget.h's ui_grid_add_back_button() doc comment. */
static void on_back(UiWidget *self, void *screen_ctx)
{
    OpenJobScreenCtx *ctx = (OpenJobScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void open_job_screen_init(OpenJobScreenCtx *ctx, UiScreenStack *stack,
                          UiScreen measure_points_screen, const ProjectContext *project_ctx,
                          JobContext *job_ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack                 = stack;
    ctx->measure_points_screen = measure_points_screen;
    ctx->project_ctx           = project_ctx;
    ctx->job_ctx                = job_ctx;

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, LIST_TOP_Y, TFT_WIDTH, (uint16_t)(LIST_BOTTOM_Y - LIST_TOP_Y)});
}

/**
 * Rebuilds the grid's job-button list from a fresh directory scan. Called
 * from on_enter (not init) -- see open_job_screen.h's file-level doc
 * comment for why the list must reflect the current on-disk state every
 * time this screen becomes visible, not just once at construction.
 *
 * The grid itself has no "remove all widgets" operation (widget.h has no
 * such function -- grids are append-only by design, see UI_GRID_MAX_WIDGETS's
 * fixed-capacity model), so this re-initializes the whole grid via
 * ui_grid_init() before re-adding the (possibly changed) set of job
 * buttons. Safe to call repeatedly: ui_grid_init() just zeroes the grid
 * struct, it does not touch anything outside it.
 */
static void rebuild_job_list(OpenJobScreenCtx *ctx)
{
    scan_jobs(ctx);

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, LIST_TOP_Y, TFT_WIDTH, (uint16_t)(LIST_BOTTOM_Y - LIST_TOP_Y)});

    uint16_t y = LIST_TOP_Y + 4;
    for (size_t i = 0; i < ctx->job_count; i++) {
        UiRect r = { LIST_MARGIN, y, LIST_BTN_W, LIST_ROW_H };
        /* ctx->job_names[i] is owned by ctx and outlives the widget, same
         * "caller-owned label" convention widget.h's file-level doc
         * comment requires -- not a readdir() dirent pointer, which would
         * already be invalid by render time. */
        UiWidget *w = ui_grid_add_button(&ctx->grid, r, ctx->job_names[i], on_job_selected);
        ui_widget_mark_scrollable(w);
        y = (uint16_t)(y + LIST_ROW_H + LIST_ROW_GAP);
    }

    /* Added last, after the job list, so ui_grid_focus_first() in
     * open_job_on_enter() below still lands on the first job button when
     * the list is non-empty -- same focus-order reasoning as
     * new_project_screen.c's and job_create_screen.c's own back-button
     * placement comments. If the list is empty, this is the only
     * focusable widget, so focus correctly lands here instead. */
    ui_grid_add_back_button(&ctx->grid, on_back);
}

static void open_job_on_enter(void *raw_ctx)
{
    OpenJobScreenCtx *ctx = (OpenJobScreenCtx *)raw_ctx;
    rebuild_job_list(ctx);
    ui_grid_focus_first(&ctx->grid); /* no-op (returns false) if job_count == 0 */
}

static bool open_job_on_event(void *raw_ctx, UiEvent ev)
{
    OpenJobScreenCtx *ctx = (OpenJobScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Job Setup */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen open_job_screen_as_ui_screen(OpenJobScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = open_job_on_enter;
    s.on_event  = open_job_on_event;
    s.on_render = open_job_screen_render;
    s.ctx       = ctx;
    return s;
}