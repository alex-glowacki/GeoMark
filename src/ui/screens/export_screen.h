/**
 * @file ui/screens/export_screen.h
 * @brief Export -- pushed from Measure Points' "Export" button. Lets
 *        the field crew write the active job's captured points to
 *        LandXML and/or CSV on local disk, the first in-UI way off
 *        the device for points captured through the new screen-stack
 *        UI (previously SCP-only, per the geomark-ui.service cutover
 *        session's "deliberately accepted gap" -- see that session's
 *        handoff notes).
 *
 * Why a separate screen rather than buttons added directly to Measure
 * Points: Measure Points' own layout is explicitly FIXED at full panel
 * height (PANEL_TOP_Y..PANEL_BOTTOM_Y) with no spare region reserved
 * for anything beyond badge/fields/Capture/readout (see
 * measure_points_screen.h's file-level doc comment) -- export needs a
 * job-name/point-count display, two format buttons, AND a result
 * message, more than the few dozen free pixels below that screen's
 * existing readout could honestly hold without cramming. Same
 * "destination screen is its own focused thing, reached via one more
 * button" pattern Pick Code's code-picker OVERLAY already uses for a
 * smaller version of the same problem -- except export's result
 * message needs to persist after the action (an overlay that
 * auto-closes on selection, like the code picker, would hide the very
 * confirmation the crew needs to see), so this is a pushed screen, not
 * another overlay.
 *
 * Reached via JobContext, not a per-push payload -- same reasoning as
 * every other job-scoped screen in this codebase (job_context.h's own
 * doc comment). Re-reads job.ini and points.csv fresh on every
 * on_enter, the same "read the real on-disk state every time this
 * screen becomes visible" stance measure_points_screen.c's
 * reload_job_data() and open_job_screen.c's rebuild_job_list() already
 * each established -- export must reflect whatever has actually been
 * captured and saved, not a stale in-memory copy from whenever this
 * screen was first constructed.
 *
 * Export destination: fixed paths under the job directory
 * (job_dir/export/points.xml, job_dir/export/points_export.csv -- see
 * collector/measure_points_export.h), never a user-typed filename.
 * Keeping this screen keyboard-free means there is exactly one
 * sensible export per job per format (re-exporting after capturing
 * more points simply overwrites the previous file, matching
 * points.csv's own "this job has exactly one of these" precedent) and
 * avoids opening the keyboard overlay for a task that is about
 * getting data OFF the device, not naming it.
 */

#ifndef GEOMARK_UI_SCREENS_EXPORT_SCREEN_H
#define GEOMARK_UI_SCREENS_EXPORT_SCREEN_H

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/job_context.h"

typedef enum {
    EXPORT_SCREEN_STATUS_NONE = 0,
    EXPORT_SCREEN_STATUS_NO_JOB,        /* JobContext has no job set */
    EXPORT_SCREEN_STATUS_LOAD_ERROR,    /* points.csv exists but failed to parse */
    EXPORT_SCREEN_STATUS_LANDXML_OK,    /* LandXML export just succeeded */
    EXPORT_SCREEN_STATUS_LANDXML_ERROR, /* LandXML export just failed (I/O) */
    EXPORT_SCREEN_STATUS_CSV_OK,        /* CSV export just succeeded */
    EXPORT_SCREEN_STATUS_CSV_ERROR,     /* CSV export just failed (I/O) */
} ExportScreenStatus;

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;      /* not owned */
    const JobContext *job_ctx; /* not owned; which job's points to export */

    gm_job_metadata_t job_meta; /* loaded from job.ini on enter */
    MeasurePointStore points;   /* loaded from points.csv on enter */

    ExportScreenStatus status;
} ExportScreenCtx;

/**
 * job_ctx is not owned and must outlive this screen -- supplies the
 * active job's resolved directory (see job_context.h).
 */
void export_screen_init(ExportScreenCtx *ctx, UiScreenStack *stack, const JobContext *job_ctx);

/** Render implementation — export_screen_draw.c (depends on ui/tft/display.h). */
void export_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen export_screen_as_ui_screen(ExportScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_EXPORT_SCREEN_H */