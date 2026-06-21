/**
 * @file ui/screens/new_project_screen.h
 * @brief New Project screen — the first real form on the widget/screen-
 *        stack foundation. Collects a project name via the on-screen
 *        keyboard (ui/core/keyboard.h) and creates
 *        ~/geomark-data/projects/<name>/ on Create.
 *
 * Layout: project name label + text field in the top ~190px, the on-screen
 * QWERTY keyboard (see ui/core/keyboard.h) filling the bottom
 * KEYBOARD_HEIGHT pixels, and a Create button between them. Per the
 * project's imperial-units convention this screen has no numeric fields
 * of its own (a project has no units yet -- units come per-job, not
 * decided here), so units.h is not involved in this screen.
 *
 * Where this fits in the nav tree: pushed by Main Menu's "Start New
 * Project" button (see ui/preview.c, which wires the real screen in where
 * a PlaceholderScreenCtx stub used to sit). Create pushes Job Setup --
 * the real job_setup_screen as of this session.
 *
 * On a successful Create, this screen also writes the project name into
 * the caller-supplied ProjectContext (see project_context.h) -- this is
 * what lets Job Create and Open Existing Job create/list jobs under the
 * actual project just created instead of a hardcoded placeholder. See
 * job_create_screen.c's historical JOB_CREATE_PLACEHOLDER_PROJECT comment
 * for the gap this closes.
 */

#ifndef GEOMARK_UI_SCREENS_NEW_PROJECT_SCREEN_H
#define GEOMARK_UI_SCREENS_NEW_PROJECT_SCREEN_H

#include "ui/core/keyboard.h"
#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/project_context.h"

#define NEW_PROJECT_NAME_MAX 32

typedef enum {
    NEW_PROJECT_STATUS_NONE = 0,
    NEW_PROJECT_STATUS_EMPTY_NAME, /* Create pressed with an empty name */
    NEW_PROJECT_STATUS_ALREADY_EXISTS,
    NEW_PROJECT_STATUS_IO_ERROR,
} NewProjectStatus;

typedef struct {
    /* MUST be first -- see ui/core/keyboard.h's file-level doc comment.
     * Every widget in this screen's grid (the name field, Create button,
     * and every keyboard key) receives the same grid->screen_ctx pointer;
     * this struct starting with UiKeyboardTarget is what makes that
     * pointer simultaneously valid as both (UiKeyboardTarget *) inside
     * keyboard.c and (NewProjectScreenCtx *) inside this screen's own
     * on_activate callbacks below. */
    UiKeyboardTarget kb;

    UiWidgetGrid grid;
    UiKeyboardLabels kb_labels;

    UiScreenStack *stack;        /* not owned */
    UiScreen job_setup_screen;   /* pushed by Create on success */
    ProjectContext *project_ctx; /* not owned; set on Create success */

    char name_buf[NEW_PROJECT_NAME_MAX];
    size_t name_len;

    /* Set by on_create's path-creation attempt, read by
     * new_project_screen_render() to show feedback. Cleared the moment
     * the name actually changes (compared by length, not content --
     * name_len_at_status snapshots ctx->name_len at the moment status was
     * set; any edit changes name_len, including a same-length
     * character swap going through Del-then-retype, which always dips
     * the length by at least one in between) so a stale error doesn't
     * linger once the person starts correcting the name, without making
     * it flash-and-vanish on its own first render. */
    NewProjectStatus status;
    size_t name_len_at_status;
} NewProjectScreenCtx;

/**
 * job_setup_screen is caller-supplied, same convention
 * main_menu_screen_init() already uses for its own not-yet-built
 * destinations -- today it's the real job_setup_screen. project_ctx is
 * not owned and must outlive this screen; it is written to (via
 * project_context_set()) only on a successful Create.
 */
void new_project_screen_init(NewProjectScreenCtx *ctx, UiScreenStack *stack,
                             UiScreen job_setup_screen, ProjectContext *project_ctx);

/** Render implementation — new_project_screen_draw.c (depends on ui/tft/display.h). */
void new_project_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen new_project_screen_as_ui_screen(NewProjectScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_NEW_PROJECT_SCREEN_H */