/**
 * @file ui/screens/job_create_screen.c
 * @brief Create New Job screen logic. No ui/tft/display.h dependency
 *        (other than through ui/core/keyboard.h's KEYBOARD_TOP_Y/HEIGHT,
 *        plain constants, not function calls) -- same split as every
 *        other screen, so this stays unit-testable on host.
 */

#define _GNU_SOURCE

#include "ui/screens/job_create_screen.h"

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
 *
 * Scroll region: from just below the title+status (y=44) to just above
 * the keyboard (KEYBOARD_TOP_Y, 248) -- see job_create_screen.h's
 * file-level doc comment for why scrolling is needed at all (11 rows,
 * ~204px available, only room for 3-4 at once).
 * ---------------------------------------------------------------------- */

#define FORM_MARGIN     20
#define FORM_FIELD_W    500
#define FORM_LABEL_H    18
#define FORM_FIELD_H    34
#define FORM_ROW_GAP    8
#define FORM_ROW_H      (FORM_LABEL_H + FORM_FIELD_H + FORM_ROW_GAP)

#define SCROLL_TOP_Y    44  /* leaves room for the title (y=8..22) and a
                              * status message (y=30..37) above the form,
                              * see job_create_screen_draw.c */
#define SCROLL_HEIGHT   (KEYBOARD_TOP_Y - SCROLL_TOP_Y)

/* -------------------------------------------------------------------------
 * Dropdown option tables
 * ---------------------------------------------------------------------- */

static const char *const COORD_SYS_OPTIONS[] = {
    "WGS84 Geographic",
    "UTM (auto zone)",
    "Local / Site Ground",
    "NAD83 ND North (EPSG:2265)",
};
#define COORD_SYS_OPTION_COUNT 4

static const char *const DIST_UNIT_OPTIONS[] = {
    "US Survey Foot",
    "International Foot",
};
#define DIST_UNIT_OPTION_COUNT 2

static const char *const COGO_OPTIONS[] = {
    "Ground",
    "Grid",
};
#define COGO_OPTION_COUNT 2

/* -------------------------------------------------------------------------
 * Job directory creation -- same three-level mkdir pattern
 * new_project_screen.c's create_project_dir() already uses, one level
 * deeper (adds /<job> under the already-created /<project>). project_name
 * is the real project from the caller's ProjectContext (see
 * job_create_screen.h's file-level doc comment) -- on_create() below
 * confirms a project is actually set before this is ever called.
 * ---------------------------------------------------------------------- */

static bool mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        log_warn("job_create: cannot create directory %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

/**
 * project_name is the actual project created by New Project, supplied via
 * the caller's ProjectContext (see job_create_screen.h's file-level doc
 * comment) -- the JOB_CREATE_PLACEHOLDER_PROJECT seam this function used
 * to hardcode is gone; the caller (on_create() below) is responsible for
 * confirming a project is actually set before calling this.
 */
static int create_job_dir(const char *project_name, const char *job_name,
                          char *out_dir, size_t out_dir_len)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        log_error("job_create: HOME is not set -- cannot resolve ~/geomark-data");
        return -1;
    }

    char base[256];
    char projects[280];
    char project[320];
    char jobs[344];
    char job[376];

    snprintf(base,     sizeof(base),     "%s/geomark-data", home);
    snprintf(projects, sizeof(projects), "%s/projects", base);
    snprintf(project,  sizeof(project),  "%s/%s", projects, project_name);
    snprintf(jobs,      sizeof(jobs),     "%s", project); /* jobs live directly under the project dir */
    snprintf(job,        sizeof(job),      "%s/%s", jobs, job_name);

    if (!mkdir_if_missing(base))     return -1;
    if (!mkdir_if_missing(projects)) return -1;
    if (!mkdir_if_missing(project))  return -1;

    struct stat st;
    bool already_existed = (stat(job, &st) == 0);

    if (!mkdir_if_missing(job)) return -1;

    snprintf(out_dir, out_dir_len, "%s", job);
    return already_existed ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Keyboard field switching -- repoints kb.buf/kb.len at whichever text
 * field was just activated. Unlike New Project (one field, set once at
 * init), this screen has six and must switch on every field activation.
 * ---------------------------------------------------------------------- */

static void set_active_field(JobCreateScreenCtx *ctx, JobCreateActiveField field)
{
    ctx->active_field = field;

    switch (field) {
    case JOB_CREATE_FIELD_JOB_NAME:
        ctx->kb.buf     = ctx->meta.job_name;
        ctx->kb.buf_cap = sizeof(ctx->meta.job_name);
        ctx->kb.len      = &ctx->job_name_len;
        break;
    case JOB_CREATE_FIELD_TEMPLATE:
        ctx->kb.buf     = ctx->meta.template_name;
        ctx->kb.buf_cap = sizeof(ctx->meta.template_name);
        ctx->kb.len      = &ctx->template_len;
        break;
    case JOB_CREATE_FIELD_REFERENCE:
        ctx->kb.buf     = ctx->meta.reference;
        ctx->kb.buf_cap = sizeof(ctx->meta.reference);
        ctx->kb.len      = &ctx->reference_len;
        break;
    case JOB_CREATE_FIELD_DESCRIPTION:
        ctx->kb.buf     = ctx->meta.description;
        ctx->kb.buf_cap = sizeof(ctx->meta.description);
        ctx->kb.len      = &ctx->description_len;
        break;
    case JOB_CREATE_FIELD_OPERATOR:
        ctx->kb.buf     = ctx->meta.operator_name;
        ctx->kb.buf_cap = sizeof(ctx->meta.operator_name);
        ctx->kb.len      = &ctx->operator_len;
        break;
    case JOB_CREATE_FIELD_NOTES:
        ctx->kb.buf     = ctx->meta.notes;
        ctx->kb.buf_cap = sizeof(ctx->meta.notes);
        ctx->kb.len      = &ctx->notes_len;
        break;
    case JOB_CREATE_FIELD_NONE:
    default:
        ctx->kb.buf = NULL;
        ctx->kb.len  = NULL;
        break;
    }
}

/* -------------------------------------------------------------------------
 * Widget callbacks
 * ---------------------------------------------------------------------- */

static void on_job_name_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_JOB_NAME);
}
static void on_template_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_TEMPLATE);
}
static void on_reference_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_REFERENCE);
}
static void on_description_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_DESCRIPTION);
}
static void on_operator_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_OPERATOR);
}
static void on_notes_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    set_active_field((JobCreateScreenCtx *)screen_ctx, JOB_CREATE_FIELD_NOTES);
}

/**
 * Coord. Sys. cycles forward on every activation (the grid's default
 * dropdown behavior, already applied by ui_grid_handle_event() before
 * this callback runs). If the new selection is ND North, force-lock the
 * Units dropdown to International Foot -- see job_create_screen.h's
 * file-level doc comment for why this is a correctness requirement, not
 * a UI nicety. There is no "disabled" widget state in the grid model
 * (see ui/core/widget.h), so "locking" here means: every time the units
 * dropdown is itself activated while ND North is selected, snap it
 * straight back to International Foot in on_dist_unit_activate() below,
 * rather than letting it cycle away even transiently.
 */
static void on_coord_sys_activate(UiWidget *self, void *screen_ctx)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)screen_ctx;
    ctx->meta.coord_sys = (gm_coord_sys_t)self->as.dropdown.selected;

    if (ctx->meta.coord_sys == GM_COORD_SYS_ND_NORTH) {
        ctx->meta.dist_unit = GM_DIST_UNIT_INTL_FOOT;
        if (ctx->dist_unit_dropdown)
            ctx->dist_unit_dropdown->as.dropdown.selected = (uint32_t)GM_DIST_UNIT_INTL_FOOT;
    }
}

static void on_dist_unit_activate(UiWidget *self, void *screen_ctx)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)screen_ctx;

    if (ctx->meta.coord_sys == GM_COORD_SYS_ND_NORTH) {
        /* Locked -- snap straight back regardless of what the grid's
         * default cycle-forward behavior just set it to. */
        self->as.dropdown.selected = (uint32_t)GM_DIST_UNIT_INTL_FOOT;
        ctx->meta.dist_unit = GM_DIST_UNIT_INTL_FOOT;
        return;
    }

    ctx->meta.dist_unit = (gm_dist_unit_t)self->as.dropdown.selected;
}

static void on_cogo_activate(UiWidget *self, void *screen_ctx)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)screen_ctx;
    ctx->meta.cogo = (gm_cogo_t)self->as.dropdown.selected;
}

static void on_create(UiWidget *self, void *screen_ctx)
{
    (void)self;
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)screen_ctx;

    if (ctx->job_name_len == 0) {
        ctx->status = JOB_CREATE_STATUS_EMPTY_NAME;
        ctx->status_job_name_len_snapshot = ctx->job_name_len;
        return;
    }

    if (!ctx->project_ctx || !project_context_has_project(ctx->project_ctx)) {
        log_error("job_create: no active project set -- cannot resolve job directory");
        ctx->status = JOB_CREATE_STATUS_NO_PROJECT;
        ctx->status_job_name_len_snapshot = ctx->job_name_len;
        return;
    }

    char job_dir[400];
    int rc = create_job_dir(ctx->project_ctx->name, ctx->meta.job_name, job_dir, sizeof(job_dir));
    if (rc < 0) {
        ctx->status = JOB_CREATE_STATUS_IO_ERROR;
        ctx->status_job_name_len_snapshot = ctx->job_name_len;
        return;
    }

    char ini_path[440];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", job_dir);

    gm_status_t save_rc = job_metadata_save(ini_path, &ctx->meta);
    if (save_rc != GM_OK) {
        ctx->status = JOB_CREATE_STATUS_IO_ERROR;
        ctx->status_job_name_len_snapshot = ctx->job_name_len;
        return;
    }

    log_info("job_create: created job '%s' at %s", ctx->meta.job_name, job_dir);

    if (ctx->job_ctx) {
        const char *home = getenv("HOME");
        if (home && home[0] != '\0')
            job_context_set(ctx->job_ctx, home, ctx->project_ctx->name, ctx->meta.job_name);
    }

    ctx->status = JOB_CREATE_STATUS_NONE;
    ui_stack_push(ctx->stack, ctx->measure_points_screen);
}

static void on_keyboard_done(void *screen_ctx)
{
    /* Same convention as New Project: Done just drops keyboard focus,
     * it does not submit. Land back on whichever field was active --
     * there is nothing else to do, since the buffer is already the
     * field that was being edited. */
    (void)screen_ctx;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void job_create_screen_init(JobCreateScreenCtx *ctx, UiScreenStack *stack,
                            UiScreen measure_points_screen, ProjectContext *project_ctx,
                            JobContext *job_ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack                  = stack;
    ctx->measure_points_screen  = measure_points_screen;
    ctx->project_ctx            = project_ctx;
    ctx->job_ctx                = job_ctx;

    job_metadata_defaults(&ctx->meta);

    ctx->kb.on_done    = on_keyboard_done;
    ctx->kb.screen_ctx = ctx;

    ui_grid_init(&ctx->grid, ctx);
    ui_grid_set_scroll_region(&ctx->grid, (UiRect){0, SCROLL_TOP_Y, 800, SCROLL_HEIGHT});

    uint16_t y = SCROLL_TOP_Y + 4;

    /* Job name */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Job Name"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Job Name", ctx->meta.job_name, sizeof(ctx->meta.job_name));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_job_name_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Template */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Template"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Template", ctx->meta.template_name, sizeof(ctx->meta.template_name));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_template_activate;
    }
    ctx->template_len = strlen(ctx->meta.template_name); /* "Default" pre-filled by job_metadata_defaults() */
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Properties heading */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Properties"));
    y = (uint16_t)(y + FORM_LABEL_H + FORM_ROW_GAP);

    /* Coord. Sys. */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Coord. Sys."));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    ctx->coord_sys_dropdown = ui_grid_add_dropdown(
        &ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H}, "Coord. Sys.",
        COORD_SYS_OPTIONS, COORD_SYS_OPTION_COUNT, (uint32_t)ctx->meta.coord_sys);
    ui_widget_mark_scrollable(ctx->coord_sys_dropdown);
    if (ctx->coord_sys_dropdown)
        ctx->coord_sys_dropdown->on_activate = on_coord_sys_activate;
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Units (Dist.) */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Units (Dist.)"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    ctx->dist_unit_dropdown = ui_grid_add_dropdown(
        &ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H}, "Units (Dist.)",
        DIST_UNIT_OPTIONS, DIST_UNIT_OPTION_COUNT, (uint32_t)ctx->meta.dist_unit);
    ui_widget_mark_scrollable(ctx->dist_unit_dropdown);
    if (ctx->dist_unit_dropdown)
        ctx->dist_unit_dropdown->on_activate = on_dist_unit_activate;
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Cogo */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Cogo"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_dropdown(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                          "Cogo", COGO_OPTIONS, COGO_OPTION_COUNT,
                                          (uint32_t)ctx->meta.cogo);
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_cogo_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Reference */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Reference"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Reference", ctx->meta.reference, sizeof(ctx->meta.reference));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_reference_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Description */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Description"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Description", ctx->meta.description, sizeof(ctx->meta.description));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_description_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Operator */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Operator"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Operator", ctx->meta.operator_name, sizeof(ctx->meta.operator_name));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_operator_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Notes */
    ui_widget_mark_scrollable(
        ui_grid_add_label(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, FORM_LABEL_H}, "Notes"));
    y = (uint16_t)(y + FORM_LABEL_H + 2);
    {
        UiWidget *w = ui_grid_add_text_field(&ctx->grid, (UiRect){FORM_MARGIN, y, FORM_FIELD_W, FORM_FIELD_H},
                                             "Notes", ctx->meta.notes, sizeof(ctx->meta.notes));
        ui_widget_mark_scrollable(w);
        if (w) w->on_activate = on_notes_activate;
    }
    y = (uint16_t)(y + FORM_FIELD_H + FORM_ROW_GAP);

    /* Create button */
    {
        UiWidget *w = ui_grid_add_button(&ctx->grid, (UiRect){FORM_MARGIN, y, 200, 44},
                                         "Create Job", on_create);
        ui_widget_mark_scrollable(w);
    }

    /* The job name field is the first focusable widget added -- start
     * editing it by default, same convention New Project uses. */
    set_active_field(ctx, JOB_CREATE_FIELD_JOB_NAME);

    keyboard_add_to_grid(&ctx->grid, &ctx->kb_labels);
}

static void job_create_on_enter(void *raw_ctx)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)raw_ctx;
    ui_grid_focus_first(&ctx->grid);
}

static bool job_create_on_event(void *raw_ctx, UiEvent ev)
{
    JobCreateScreenCtx *ctx = (JobCreateScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK)
        return false; /* unconsumed -- stack pops back to Job Setup */

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen job_create_screen_as_ui_screen(JobCreateScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = job_create_on_enter;
    s.on_event  = job_create_on_event;
    s.on_render = job_create_screen_render;
    s.ctx       = ctx;
    return s;
}