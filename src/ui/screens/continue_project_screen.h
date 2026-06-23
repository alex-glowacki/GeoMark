/**
 * @file ui/screens/continue_project_screen.h
 * @brief Continue Existing Project — lists project directories under
 *        ~/geomark-data/projects/, per main_menu_screen.h's "Continue
 *        Existing Project" doc comment (the previous PlaceholderScreenCtx
 *        stub this screen replaces) and job_setup_screen.h's own doc
 *        comment, which already anticipated this screen as the second
 *        way to reach Job Setup (the first being New Project's Create
 *        button).
 *
 * Layout, scan timing, and scroll behavior all mirror open_job_screen.h
 * exactly -- same UiWidgetGrid scroll_region pattern, same "rescan on
 * every on_enter, don't cache" stance, same status_text()-in-the-draw
 * -file convention. The only structural difference is what selecting an
 * item does: open_job_screen.h loads a job.ini and pushes Measure
 * Points; this screen instead writes the selected project's name into
 * the shared ProjectContext (see project_context.h) and pushes Job
 * Setup -- selecting a project here is equivalent to typing that name
 * into New Project and pressing Create, except the directory already
 * exists.
 *
 * This is also the fix for a real gap discovered during hardware
 * testing: ProjectContext lives only in memory for the lifetime of one
 * geomark process. Restarting the UI (e.g. Ctrl+C during testing, or a
 * crash/reboot in the field) loses track of which project was active,
 * even though that project's directory and any jobs already created
 * under it are still sitting on disk untouched. Before this screen
 * existed, there was no way back to those jobs except recreating a
 * project with the exact same name (which New Project's
 * NEW_PROJECT_STATUS_ALREADY_EXISTS path handles, but doesn't select it
 * as active either) -- Continue Existing Project closes that gap.
 *
 * If ~/geomark-data/projects/ doesn't exist yet or has no subdirectories
 * (a fresh install, or a person who has only ever used Continue Project
 * before any New Project), the list area shows a message instead of any
 * buttons -- same "nothing to select" handling open_job_screen.h uses
 * for an empty job list.
 */

#ifndef GEOMARK_UI_SCREENS_CONTINUE_PROJECT_SCREEN_H
#define GEOMARK_UI_SCREENS_CONTINUE_PROJECT_SCREEN_H

#include <stddef.h>
#include <stdint.h>

#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/project_context.h"

/**
 * Upper bound on listed projects -- same rationale as
 * OPEN_JOB_MAX_LISTED in open_job_screen.h (each project is one button
 * widget, bounded by UI_GRID_MAX_WIDGETS regardless; 40 is a generous
 * real-world ceiling, not a tuned value).
 */
#define CONTINUE_PROJECT_MAX_LISTED 40

typedef enum {
    CONTINUE_PROJECT_STATUS_NONE = 0,
    CONTINUE_PROJECT_STATUS_NO_PROJECTS, /* ~/geomark-data/projects has zero subdirs (or doesn't
                                            exist) */
} ContinueProjectStatus;

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;        /* not owned */
    UiScreen job_setup_screen;   /* pushed after a project is selected */
    ProjectContext *project_ctx; /* not owned; written to on selection (unlike
                                  * open_job_screen.h's read-only project_ctx) */

    /* Project names discovered by the most recent on_enter scan -- same
     * "caller-owned label outlives a readdir() dirent" rationale as
     * open_job_screen.h's job_names[][]. */
    char project_names[CONTINUE_PROJECT_MAX_LISTED][PROJECT_CONTEXT_NAME_MAX];
    size_t project_count;

    ContinueProjectStatus status;

    /**
     * grid.focus_idx captured by continue_project_on_event() immediately
     * before forwarding an event to ui_grid_handle_event() -- identical
     * mechanism and rationale to open_job_screen.h's scroll_anchor_idx
     * (see that header's doc comment for the full explanation of why
     * this is necessary: UI_EVENT_TAP relocates focus onto a tapped nav
     * button before firing its on_activate, so without restoring this
     * saved value first, Up/Down would always search outward from the
     * button's own position rather than from wherever the person was
     * actually scrolling). -1 means "nothing focused yet".
     */
    int32_t scroll_anchor_idx;
} ContinueProjectScreenCtx;

/**
 * project_ctx is not owned and must outlive this screen -- written to
 * (via project_context_set()) when a project is selected, same
 * "caller-supplied dependency" convention every other cross-screen
 * relationship in this codebase uses.
 */
void continue_project_screen_init(ContinueProjectScreenCtx *ctx, UiScreenStack *stack,
                                  UiScreen job_setup_screen, ProjectContext *project_ctx);

/** Render implementation — continue_project_screen_draw.c (depends on ui/tft/display.h). */
void continue_project_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen continue_project_screen_as_ui_screen(ContinueProjectScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_CONTINUE_PROJECT_SCREEN_H */