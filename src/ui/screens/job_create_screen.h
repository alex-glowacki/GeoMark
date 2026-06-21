/**
 * @file ui/screens/job_create_screen.h
 * @brief Create New Job — the Properties form, per
 *        geomark-ui-redesign-decisions.md §2: Job name, Template,
 *        and a Properties section (Coord. sys., Units (Dist.), Cogo,
 *        Reference, Description, Operator, Notes).
 *
 * Layout: fixed title at the top, a D-pad-scrollable form region below it
 * (see ui/core/widget.h's scroll_region support -- 11 rows don't fit in
 * the ~204px available above the keyboard at once), and the always-
 * visible on-screen keyboard (ui/core/keyboard.h) filling the bottom
 * KEYBOARD_HEIGHT pixels, same pattern New Project already established.
 *
 * Coordinate-system / units coupling: selecting GM_COORD_SYS_ND_NORTH
 * (the only NAD83 State Plane zone GeoMark supports, North Dakota North,
 * EPSG:2265) auto-locks the Units (Dist.) dropdown to International Foot
 * and disables it -- that zone's coordinate unit is fixed by its own
 * legal/EPSG definition, not a free per-job choice. See
 * job_metadata.h's gm_dist_unit_t doc comment for why getting this wrong
 * is a real correctness error, not a style preference.
 *
 * On Create: writes job.ini via job_metadata_save() under
 * ~/geomark-data/projects/<project>/<job>/, records the new job in the
 * caller-supplied JobContext (job_context.h) so Measure Points knows
 * which job it was pushed for, then pushes the caller-supplied
 * measure_points_screen. <project> comes from the caller-supplied
 * ProjectContext (see project_context.h) -- the actual project name New
 * Project created, not a hardcoded placeholder (see this header's
 * history for the JOB_CREATE_PLACEHOLDER_PROJECT seam this replaced). If
 * project_ctx has no project set (e.g. this screen reached directly,
 * bypassing New Project), Create reports JOB_CREATE_STATUS_NO_PROJECT
 * rather than silently writing somewhere wrong.
 */

#ifndef GEOMARK_UI_SCREENS_JOB_CREATE_SCREEN_H
#define GEOMARK_UI_SCREENS_JOB_CREATE_SCREEN_H

#include <stddef.h>

#include "collector/job_metadata.h"
#include "ui/core/keyboard.h"
#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/job_context.h"
#include "ui/screens/project_context.h"

typedef enum {
    JOB_CREATE_STATUS_NONE = 0,
    JOB_CREATE_STATUS_EMPTY_NAME,
    JOB_CREATE_STATUS_IO_ERROR,
    JOB_CREATE_STATUS_NO_PROJECT,
} JobCreateStatus;

/**
 * Which text field the keyboard is currently feeding. Job Setup has six
 * text fields (job name, template, reference, description, operator,
 * notes) sharing one on-screen keyboard -- unlike New Project's single
 * field, this screen does need to switch kb.buf/kb.len when focus moves
 * to a different field, which happens in the widget grid's own
 * UI_EVENT_TAP/ACTIVATE handling via each field's on_activate callback
 * (see job_create_screen.c's on_field_activate()).
 */
typedef enum {
    JOB_CREATE_FIELD_NONE = 0,
    JOB_CREATE_FIELD_JOB_NAME,
    JOB_CREATE_FIELD_TEMPLATE,
    JOB_CREATE_FIELD_REFERENCE,
    JOB_CREATE_FIELD_DESCRIPTION,
    JOB_CREATE_FIELD_OPERATOR,
    JOB_CREATE_FIELD_NOTES,
} JobCreateActiveField;

typedef struct {
    /* MUST be first -- see ui/core/keyboard.h's file-level doc comment. */
    UiKeyboardTarget kb;

    UiWidgetGrid grid;
    UiKeyboardLabels kb_labels;

    UiScreenStack *stack;           /* not owned */
    UiScreen measure_points_screen; /* pushed by Create on success */
    ProjectContext *project_ctx;    /* not owned; read-only here */
    JobContext *job_ctx;            /* not owned; written on successful Create
                                     * -- see job_context.h for why Measure
                                     * Points needs this set before it's
                                     * pushed */

    gm_job_metadata_t meta;

    /* Length counters for the metadata struct's own fixed-size char
     * arrays -- the keyboard's UiKeyboardTarget.len needs a pointer to
     * an externally-tracked length (it does not itself call strlen()
     * every key, see keyboard.h), and meta's fields are plain char[]
     * with no length field of their own. One counter per text field,
     * repointed-to by kb.len whenever that field becomes active. */
    size_t job_name_len;
    size_t template_len;
    size_t reference_len;
    size_t description_len;
    size_t operator_len;
    size_t notes_len;

    JobCreateActiveField active_field;

    /* Widget indices for the two dropdowns whose options depend on each
     * other (coord_sys locks/unlocks dist_unit) -- cached at init time
     * so on_coord_sys_activate() can reach the dist_unit widget directly
     * without a linear search through the grid on every activation. */
    UiWidget *coord_sys_dropdown;
    UiWidget *dist_unit_dropdown;

    JobCreateStatus status;
    size_t status_job_name_len_snapshot; /* see new_project_screen.h's
                                          * identical pattern for why */
} JobCreateScreenCtx;

/**
 * project_ctx is not owned and must outlive this screen -- it supplies
 * the actual project name jobs are created under (see project_context.h).
 * job_ctx is not owned and must outlive this screen -- on a successful
 * Create, this screen writes the new job's name/directory into it (see
 * job_context.h) so Measure Points knows which job it was pushed for.
 */
void job_create_screen_init(JobCreateScreenCtx *ctx, UiScreenStack *stack,
                            UiScreen measure_points_screen, ProjectContext *project_ctx,
                            JobContext *job_ctx);

/** Render implementation — job_create_screen_draw.c (depends on ui/tft/display.h). */
void job_create_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen job_create_screen_as_ui_screen(JobCreateScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_JOB_CREATE_SCREEN_H */