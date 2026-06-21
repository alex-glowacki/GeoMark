/**
 * @file ui/screens/measure_points_screen.h
 * @brief Measure Points — the screen Job Create and Open Existing Job
 *        both push into. Layout: left ~2/3 a live map panel plotting
 *        captured points, right ~1/3 an RTK status display and a
 *        Capture Point action.
 *
 * Reached via JobContext (job_context.h), not a per-push payload --
 * same reasoning as project_context.h/job_context.h's own doc comments:
 * UiScreen has no such mechanism, so the active job's resolved
 * directory is threaded through shared state instead, set by Job
 * Create on successful Create and by Open Existing Job on successful
 * load, read here.
 *
 * Navigation note: the physical d-pad's Right button is documented
 * (project memory) as currently-unreliable hardware, and Left is
 * permanently mapped to UI_EVENT_BACK (ui/preview.c's translate_input()).
 * That leaves Up/Down/Center as the only input this screen can rely on
 * -- so unlike a desktop two-pane layout, focus order must never
 * require a NAV_RIGHT to reach the status/capture panel. The grid here
 * has exactly one focusable widget (the Capture Point button); the map
 * panel itself is a live display, not a focus target, in this version.
 *
 * RTK feed: this screen depends on RtkFeedFn, not on net/stream_client.h
 * directly -- see this header's RtkFeed doc comment below for why.
 * measure_points_screen_init() accepts a feed; ui/preview.c supplies a
 * built-in no-op stub for now (real stream_client wiring is a separate,
 * focused follow-up).
 */

#ifndef GEOMARK_UI_SCREENS_MEASURE_POINTS_SCREEN_H
#define GEOMARK_UI_SCREENS_MEASURE_POINTS_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "geomark.h"
#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"
#include "ui/screens/job_context.h"

/* -------------------------------------------------------------------------
 * RTK feed seam
 *
 * Deliberately a narrow function-pointer interface rather than a direct
 * net/stream_client.h dependency: ui/preview.c's widget UI currently has
 * zero networking (no host argument, no socket, nothing), and the
 * legacy ui/client.c flow that *does* connect to the rover stream must
 * stay untouched. Bolting live TCP networking onto screen-layout/
 * widget-grid/capture-logic work in the same change mixes two genuinely
 * separate concerns and makes this screen impossible to unit-test on
 * host without a live rover. This interface is the same shape
 * ui/client.c's own UISharedState already uses internally (snapshot
 * under lock, read once per tick) -- lifted out as a seam instead of
 * left as that one file's private detail.
 * ---------------------------------------------------------------------- */

typedef struct {
    double lat; /* decimal degrees, WGS84, +N */
    double lon; /* decimal degrees, WGS84, +E */
    double alt; /* meters MSL */
    double hdop;
    uint8_t num_sats;
    uint8_t fix_quality; /* gm_fix_type_t value */
    bool valid;          /* false until the feed has a real fix */
} RtkFeedPosition;

/** Called once per tick. Implementations must be safe to call from the
 *  UI thread at the screen's render rate (10 Hz in ui/preview.c) --
 *  same constraint ui/client.c's packet_callback()/snapshot pattern
 *  already satisfies; a real stream_client-backed implementation should
 *  follow that same mutex-snapshot shape, not block here. */
typedef void (*RtkFeedFn)(void *user, RtkFeedPosition *out);

typedef struct {
    RtkFeedFn fn;
    void *user; /* not owned; passed through to fn unchanged */
} RtkFeed;

/**
 * The default feed: always reports valid=false. Used by ui/preview.c
 * until real stream_client wiring lands; also what tests construct a
 * screen with, since GM_OK behavior must not depend on live hardware.
 */
RtkFeed measure_points_no_feed(void);

/* -------------------------------------------------------------------------
 * Screen state
 * ---------------------------------------------------------------------- */

typedef enum {
    MEASURE_POINTS_STATUS_NONE = 0,
    MEASURE_POINTS_STATUS_NO_JOB,     /* JobContext has no job set */
    MEASURE_POINTS_STATUS_LOAD_ERROR, /* points.csv exists but failed to parse */
    MEASURE_POINTS_STATUS_STORE_FULL, /* GM_MEASURE_POINTS_MAX reached */
    MEASURE_POINTS_STATUS_NO_FIX,     /* Capture pressed with no valid fix */
} MeasurePointsStatus;

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;       /* not owned */
    const JobContext *job_ctx;  /* not owned; which job's points.csv to use */
    gm_job_metadata_t job_meta; /* loaded from job.ini on enter -- coord_sys, etc. */

    RtkFeed feed;
    RtkFeedPosition latest; /* refreshed every on_tick from feed.fn */

    MeasurePointStore points;
    bool have_origin; /* true once the first point fixes the local-fallback origin */
    double origin_lat;
    double origin_lon;

    MeasurePointsStatus status;
} MeasurePointsScreenCtx;

/**
 * job_ctx is not owned and must outlive this screen -- supplies the
 * active job's resolved directory (see job_context.h). feed is copied
 * by value (it is itself just a function pointer + opaque pointer, see
 * RtkFeed above) -- pass measure_points_no_feed() until real hardware
 * wiring exists.
 */
void measure_points_screen_init(MeasurePointsScreenCtx *ctx, UiScreenStack *stack,
                                const JobContext *job_ctx, RtkFeed feed);

/** Render implementation — measure_points_screen_draw.c (depends on ui/tft/display.h). */
void measure_points_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen measure_points_screen_as_ui_screen(MeasurePointsScreenCtx *ctx);

/* -------------------------------------------------------------------------
 * Layout constants shared between this screen's logic (the Capture
 * Point button's grid position) and measure_points_screen_draw.c (the
 * map/status panel split, divider line). Literal values rather than
 * TFT_WIDTH/TFT_HEIGHT arithmetic, so this header has no ui/tft/
 * display.h dependency -- same convention ui/core/keyboard.h's own
 * KEYBOARD_HEIGHT already established (see that header's comment: the
 * value is documented as derived from TFT_HEIGHT, not computed from it
 * at the preprocessor level here).
 *
 *   STATUS_PANEL_W/_X : right third of TFT_WIDTH (800) is the status
 *                       panel; everything left of it is the map panel.
 *   PANEL_TOP_Y/_BOTTOM_Y : vertical extent both panels share, below
 *                       the title bar and above the bottom margin.
 * ---------------------------------------------------------------------- */

#define STATUS_PANEL_W 266 /* TFT_WIDTH (800) / 3 */
#define STATUS_PANEL_X 534 /* TFT_WIDTH (800) - STATUS_PANEL_W */
#define PANEL_TOP_Y 40
#define PANEL_BOTTOM_Y 472 /* TFT_HEIGHT (480) - 8 */

#define CAPTURE_BTN_MARGIN 12
#define CAPTURE_BTN_H 56
#define CAPTURE_BTN_W 242 /* STATUS_PANEL_W - 2 * CAPTURE_BTN_MARGIN */
#define CAPTURE_BTN_Y 404 /* PANEL_BOTTOM_Y - CAPTURE_BTN_H - CAPTURE_BTN_MARGIN */

#endif /* GEOMARK_UI_SCREENS_MEASURE_POINTS_SCREEN_H */