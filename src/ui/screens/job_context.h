/**
 * @file ui/screens/job_context.h
 * @brief The piece of state shared between Job Create, Open Existing
 *        Job, and Measure Points: which job is currently active, and
 *        where its files live on disk.
 *
 * Same gap, same fix as project_context.h: UiScreen (screen_stack.h)
 * carries only a vtable + one opaque ctx pointer, fixed at screen-
 * construction time in ui/preview.c -- there is no per-push payload
 * mechanism. Job Create and Open Existing Job both already write
 * job.ini to a real on-disk job directory and then push a fixed
 * measure_points_screen UiScreen with no way to tell Measure Points
 * *which* job.ini was just written/loaded. JobContext closes that gap,
 * the same way ProjectContext closes the equivalent gap for the active
 * project's name.
 *
 * Deliberately separate from ProjectContext rather than folding the job
 * name into it -- the two pieces of state are set at different points
 * in the nav tree (a project can be active with no job chosen yet, e.g.
 * sitting on the Job Setup screen) and have different lifetimes; see
 * project_context.h's own doc comment on why one field did not justify
 * a generic "app state" struct, which applies again here rather than
 * retroactively becoming a reason to merge the two.
 *
 * job_dir is resolved and stored once, at the point job_context_set()
 * is called (Job Create on successful Create, Open Existing Job on
 * successful load) -- both of those call sites already have the
 * project name and job name in hand at that moment, and every
 * subsequent reader (Measure Points) needs the resolved directory
 * repeatedly (for points.csv reads/writes), so resolving it once here
 * avoids a fourth copy of the
 * "%s/geomark-data/projects/%s/%s" snprintf pattern job_create_screen.c
 * and open_job_screen.c already each have one of.
 *
 * Ownership: one instance lives for the lifetime of the screen stack,
 * exactly like ProjectContext (declared alongside it in ui/preview.c
 * and tests/test_screens.c), passed by pointer to every screen that
 * needs it.
 */

#ifndef GEOMARK_UI_SCREENS_JOB_CONTEXT_H
#define GEOMARK_UI_SCREENS_JOB_CONTEXT_H

#include <stdbool.h>

#include "collector/job_metadata.h"

#define JOB_CONTEXT_NAME_MAX GM_JOB_NAME_MAX

/** Matches the generous-ceiling convention of every other path buffer
 *  in this codebase (job_create_screen.c / open_job_screen.c each use
 *  similarly-sized stack buffers for the same path shape). */
#define JOB_CONTEXT_DIR_MAX 512

typedef struct {
    char name[JOB_CONTEXT_NAME_MAX];
    char job_dir[JOB_CONTEXT_DIR_MAX]; /* ~/geomark-data/projects/<project>/<job> */
} JobContext;

/** Zeroes name and job_dir -- "no job active yet". */
void job_context_init(JobContext *ctx);

/**
 * Sets the active job: stores job_name (truncated to fit if necessary,
 * same strncpy + manual NUL-terminate convention as
 * project_context_set()) and resolves job_dir from
 * "<home>/geomark-data/projects/<project_name>/<job_name>".
 *
 * home and project_name are read once here, not stored -- JobContext
 * only needs the already-resolved directory afterward, not the pieces
 * that built it (mirrors job_dir being the thing every reader actually
 * wants, the same way ProjectContext exposes the project name itself
 * rather than e.g. a raw HOME pointer).
 */
void job_context_set(JobContext *ctx, const char *home, const char *project_name,
                     const char *job_name);

/** True if a job name has been set (name[0] != '\0'). */
bool job_context_has_job(const JobContext *ctx);

#endif /* GEOMARK_UI_SCREENS_JOB_CONTEXT_H */