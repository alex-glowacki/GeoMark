/**
 * @file ui/screens/open_job_screen.h
 * @brief Open Existing Job — lists job subdirectories under the active
 *        project (~/geomark-data/projects/<project>/, one entry per
 *        job), per job_setup_screen.h's "Open Existing Job" doc comment (the
 *        previous PlaceholderScreenCtx stub this screen replaces).
 *
 * Layout: fixed title at the top, a D-pad-scrollable list of job-name
 * buttons below it -- same UiWidgetGrid scroll_region pattern
 * job_create_screen.h already uses for its Properties form (see that
 * header's doc comment for why scrolling is needed at all: a project can
 * have more jobs than fit on screen at once). No on-screen keyboard --
 * this screen has no text entry, so the full panel height below the
 * title is available for the list, unlike Job Create which reserves the
 * bottom KEYBOARD_HEIGHT pixels.
 *
 * The list is (re)built every time this screen is entered (on_enter), by
 * scanning the active project's directory with readdir() -- not cached
 * at init time, since jobs can be created (via Job Create) after Open
 * Existing Job's ctx already exists; Job Create's own job_create_screen.h
 * established the same "read the real on-disk state, don't assume" stance
 * for job persistence, this mirrors it for listing.
 *
 * On selecting a job: loads that job's job.ini via job_metadata_load()
 * into this screen's own gm_job_metadata_t and pushes the caller-supplied
 * measure_points_screen (today a PlaceholderScreenCtx stub, the same one
 * job_create_screen.h pushes on successful Create) -- this screen and Job
 * Create are siblings in the nav tree, alternate entry points into the
 * same Measure Points destination.
 *
 * If no project is active (ProjectContext empty) or the project directory
 * has no job subdirectories, the list area shows a message instead of any
 * buttons; there is nothing to select, so on_enter does not attempt to
 * focus anything in that case.
 */

#ifndef GEOMARK_UI_SCREENS_OPEN_JOB_SCREEN_H
#define GEOMARK_UI_SCREENS_OPEN_JOB_SCREEN_H

#include <stddef.h>

#include "collector/job_metadata.h"
#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/project_context.h"

/**
 * Upper bound on listed jobs per project -- matches the grid's own fixed
 * capacity concern (UI_GRID_MAX_WIDGETS, see widget.h): each job is one
 * button widget, so this can never exceed UI_GRID_MAX_WIDGETS regardless.
 * 40 is a deliberately generous real-world ceiling, not a tuned value --
 * revisit (same as UI_GRID_MAX_WIDGETS's own history of 24 -> 50 -> 80)
 * if a real project ever has more jobs than this.
 */
#define OPEN_JOB_MAX_LISTED 40

typedef enum {
    OPEN_JOB_STATUS_NONE = 0,
    OPEN_JOB_STATUS_NO_PROJECT, /* ProjectContext has no project set */
    OPEN_JOB_STATUS_NO_JOBS,    /* project dir exists, has zero job subdirs */
    OPEN_JOB_STATUS_LOAD_ERROR, /* job.ini failed to load after selection */
} OpenJobStatus;

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;              /* not owned */
    UiScreen measure_points_screen;    /* pushed after a successful job load */
    const ProjectContext *project_ctx; /* not owned; read-only here */

    /* Job names discovered by the most recent on_enter scan, owned by
     * this struct (fixed-size, no heap) -- each ui_grid_add_button() call
     * needs a label pointer that outlives the widget, so these buffers
     * back the buttons directly rather than handing the grid a pointer
     * into a readdir() dirent that's already gone by render time. */
    char job_names[OPEN_JOB_MAX_LISTED][GM_JOB_NAME_MAX];
    size_t job_count;

    gm_job_metadata_t loaded_meta; /* filled by job_metadata_load() on selection */

    OpenJobStatus status;
} OpenJobScreenCtx;

/**
 * project_ctx is not owned and must outlive this screen -- supplies which
 * project's jobs to list (see project_context.h).
 */
void open_job_screen_init(OpenJobScreenCtx *ctx, UiScreenStack *stack,
                          UiScreen measure_points_screen, const ProjectContext *project_ctx);

/** Render implementation — open_job_screen_draw.c (depends on ui/tft/display.h). */
void open_job_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen open_job_screen_as_ui_screen(OpenJobScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_OPEN_JOB_SCREEN_H */