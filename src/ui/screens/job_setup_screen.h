/**
 * @file ui/screens/job_setup_screen.h
 * @brief Job Setup — "Create New Job" or "Open Existing Job" choice, per
 *        geomark-ui-redesign-decisions.md §2. Reached from New Project's
 *        Create button, or from Continue Project -> select saved project
 *        (the latter not yet wired -- Continue Project is still a
 *        placeholder, see ui/preview.c).
 *
 * Same pattern as main_menu_screen.h: the two destination screens are
 * caller-supplied so this screen never needs to know what Job
 * Create/Open actually are. Today Open Existing Job is a
 * PlaceholderScreenCtx stub (browsing saved jobs is separate scope, its
 * own screen, not yet built) while Create New Job is the real
 * job_create_screen.
 */

#ifndef GEOMARK_UI_SCREENS_JOB_SETUP_SCREEN_H
#define GEOMARK_UI_SCREENS_JOB_SETUP_SCREEN_H

#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;       /* not owned */
    UiScreen create_job_screen; /* pushed by "Create New Job" */
    UiScreen open_job_screen;   /* pushed by "Open Existing Job" */
} JobSetupScreenCtx;

void job_setup_screen_init(JobSetupScreenCtx *ctx, UiScreenStack *stack, UiScreen create_job_screen,
                           UiScreen open_job_screen);

/** Render implementation — job_setup_screen_draw.c (depends on ui/tft/display.h). */
void job_setup_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen job_setup_screen_as_ui_screen(JobSetupScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_JOB_SETUP_SCREEN_H */