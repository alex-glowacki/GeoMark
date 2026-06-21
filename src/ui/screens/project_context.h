/**
 * @file ui/screens/project_context.h
 * @brief The one piece of state shared between New Project, Job Setup,
 *        Job Create, and Open Existing Job: which project is currently
 *        active.
 *
 * Why a separate module rather than threading the name through
 * ui_stack_push(): UiScreen (screen_stack.h) only carries a vtable + one
 * opaque ctx pointer, fixed at screen-construction time in
 * ui/preview.c -- there is no per-push payload mechanism, and adding one
 * would mean changing screen_stack.h's contract for every existing
 * screen. Every screen ctx struct already follows the "caller-supplied
 * dependency, set once at init" convention (see job_setup_screen.h's
 * create_job_screen/open_job_screen, main_menu_screen.h's three
 * destination screens); this is the same pattern applied to a small
 * piece of shared data instead of a destination screen.
 *
 * Deliberately narrow -- just the active project's name, not a generic
 * "app state" struct. Revisit if a second piece of cross-screen state
 * shows up; one field does not justify speculative generality.
 *
 * Ownership: one instance lives for the lifetime of the screen stack
 * (declared alongside the screen ctxs in ui/preview.c and
 * tests/test_screens.c), and every screen that needs it is handed a
 * pointer at init -- same "not owned" convention as UiScreenStack*.
 */

#ifndef GEOMARK_UI_SCREENS_PROJECT_CONTEXT_H
#define GEOMARK_UI_SCREENS_PROJECT_CONTEXT_H

#include <stdbool.h>

#define PROJECT_CONTEXT_NAME_MAX 32 /* matches NEW_PROJECT_NAME_MAX */

typedef struct {
    char name[PROJECT_CONTEXT_NAME_MAX];
} ProjectContext;

/** Zeroes name (empty string) -- "no project active yet". */
void project_context_init(ProjectContext *ctx);

/**
 * Sets the active project name, truncating to fit if necessary (same
 * strncpy + manual NUL-terminate convention used throughout this
 * codebase, e.g. job_metadata_load()).
 */
void project_context_set(ProjectContext *ctx, const char *name);

/** True if a project name has been set (name[0] != '\0'). */
bool project_context_has_project(const ProjectContext *ctx);

#endif /* GEOMARK_UI_SCREENS_PROJECT_CONTEXT_H */