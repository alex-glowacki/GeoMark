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
 * Export destination: the mounted USB export drive
 * (collector/usb_export.h's USB_EXPORT_MOUNT_POINT, mirroring this
 * job's <project>/<job> structure under it) when actually mounted,
 * falling back to the fixed internal path under the job directory
 * (job_dir/export/points.xml, job_dir/export/points_export.csv -- see
 * collector/measure_points_export.h) when it is not. Checked fresh on
 * every Export button press (usb_export_is_mounted(), not cached at
 * on_enter) -- a field crew plugging the drive in or pulling it out
 * mid-session, between presses, should be honored immediately rather
 * than reflecting whatever was true when this screen was last entered.
 * Never a user-typed filename in either case: re-exporting after
 * capturing more points simply overwrites the previous file at
 * whichever destination was actually used, matching points.csv's own
 * "this job has exactly one of these" precedent. The crew is told
 * which destination was actually used via a distinct fallback status
 * message (EXPORT_SCREEN_STATUS_*_OK_FALLBACK below) -- a silent
 * fallback would risk a crew believing their data is on the USB drive
 * when it is not.
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
    EXPORT_SCREEN_STATUS_NO_JOB,              /* JobContext has no job set */
    EXPORT_SCREEN_STATUS_LOAD_ERROR,          /* points.csv exists but failed to parse */
    EXPORT_SCREEN_STATUS_LANDXML_OK,          /* LandXML written to the USB drive */
    EXPORT_SCREEN_STATUS_LANDXML_OK_FALLBACK, /* LandXML written to internal storage --
                                               * USB drive was not mounted, see
                                               * collector/usb_export.h */
    EXPORT_SCREEN_STATUS_LANDXML_ERROR,       /* LandXML export failed (I/O), neither
                                               * destination */
    EXPORT_SCREEN_STATUS_CSV_OK,              /* CSV written to the USB drive */
    EXPORT_SCREEN_STATUS_CSV_OK_FALLBACK,     /* CSV written to internal storage --
                                               * USB drive was not mounted */
    EXPORT_SCREEN_STATUS_CSV_ERROR,           /* CSV export failed (I/O), neither
                                               * destination */
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