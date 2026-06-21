/**
 * @file ui/screens/measure_points_screen.h
 * @brief Measure Points — the screen Job Create and Open Existing Job
 *        both push into. Layout: left ~2/3 a live map panel plotting
 *        captured points (white background, black markers/text), right
 *        ~1/3 a fix badge, Point name / Code / Target height fields,
 *        the Capture Point action, and a compact live-fix readout --
 *        all stacked into that one column, above an always-visible
 *        on-screen keyboard filling the bottom KEYBOARD_HEIGHT pixels
 *        (same convention job_create_screen.h established: the map
 *        panel keeps its full left-2/3 width and height down to
 *        KEYBOARD_TOP_Y -- only the right column's vertical budget is
 *        tight, not the map's).
 *
 * Reached via JobContext (job_context.h), not a per-push payload --
 * same reasoning as project_context.h/job_context.h's own doc comments:
 * UiScreen has no such mechanism, so the active job's resolved
 * directory is threaded through shared state instead, set by Job
 * Create on successful Create and by Open Existing Job on successful
 * load, read here.
 *
 * Point metadata entry: Point name, Code, and Target height are typed
 * BEFORE Capture Point is pressed -- whatever is in those three fields
 * at the moment Capture fires is what gets attached to that point (see
 * on_capture_point() in the .c file). This mirrors how every other
 * field on this screen already works (the keyboard feeds whichever
 * field is currently active, same six-field pattern
 * job_create_screen.h established) and matches standard RTK field
 * practice: target height in particular has to be correct *before* the
 * shot, since it directly corrects the measured elevation (see
 * measure_points.h's MeasurePoint doc comment).
 *
 * Point name auto-increment: if, at the moment Capture succeeds, the
 * Point name field's content parses entirely as a non-negative integer
 * (e.g. "1", "42") -- checked with the same strtol()-and-check-the-
 * endptr technique as any other "is this string purely numeric"
 * check -- the field is advanced to that integer + 1 for the next shot
 * ("1" -> "2"). Any non-purely-numeric name (e.g. "BENCHMARK_A") is
 * left untouched, since there's no sane "next" value for it -- this is
 * the "unless changed manually" half of the auto-increment rule.
 *
 * Target height unit: entered and displayed in feet (international
 * foot, matching units.h's existing convention that vertical
 * measurements use the international foot, not the US survey foot --
 * see units.h's own file-level doc comment), converted to meters via
 * gm_intl_ft_to_m() before being stored in MeasurePoint::target_height_m
 * (which is meters, matching this codebase's all-internal-SI
 * convention). Persists across captures by design (typed once, stays
 * until changed) -- a field crew's rod height rarely changes during a
 * session, so resetting it every shot would just mean re-typing the
 * same number repeatedly.
 *
 * Navigation note: the physical d-pad's Right button is documented
 * (project memory) as currently-unreliable hardware, and Left is
 * permanently mapped to UI_EVENT_BACK (ui/preview.c's translate_input()).
 * That leaves Up/Down/Center as the only input this screen can rely on
 * for keyboard-less navigation -- the keyboard's own Left/Right-within-
 * a-row limitation (see ui/core/keyboard.h's file-level doc comment) is
 * a pre-existing, documented scope limit this screen inherits, not a
 * new one. The map panel itself is a live display, not a focus target.
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
#include <stddef.h>
#include <stdint.h>

#include "collector/job_metadata.h"
#include "collector/measure_points.h"
#include "geomark.h"
#include "ui/core/keyboard.h"
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
    double alt; /* meters MSL -- raw antenna altitude, NOT
                 * target-height corrected (correction happens
                 * at capture time, see on_capture_point()) */
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

/** Limits for the typed Target height field's own text buffer -- plenty
 *  for any realistic rod height ("6.562", "-0.5", etc.) typed through
 *  the keyboard's digit/'-' keys. Parsed via atof() at capture time
 *  (same string-to-double convention gnss/nmea.c already uses
 *  throughout), not kept as a live double the keyboard writes into --
 *  the keyboard module only ever mutates char buffers (see
 *  ui/core/keyboard.h's UiKeyboardTarget), the same reason every other
 *  keyboard-fed field on every other screen in this codebase
 *  (job_create_screen.h's six text fields, etc.) is itself a char[],
 *  parsed/interpreted by its owning screen, not a typed value the
 *  keyboard understands directly. */
#define MEASURE_POINTS_HEIGHT_BUF_MAX 16

/**
 * Which text field the keyboard is currently feeding -- same pattern
 * job_create_screen.h's JobCreateActiveField already established for
 * its six fields, here for this screen's three.
 */
typedef enum {
    MEASURE_POINTS_FIELD_NONE = 0,
    MEASURE_POINTS_FIELD_NAME,
    MEASURE_POINTS_FIELD_CODE,
    MEASURE_POINTS_FIELD_HEIGHT,
} MeasurePointsActiveField;

typedef struct {
    /* MUST be first -- see ui/core/keyboard.h's file-level doc comment. */
    UiKeyboardTarget kb;

    UiWidgetGrid grid;
    UiKeyboardLabels kb_labels;

    UiScreenStack *stack;       /* not owned */
    const JobContext *job_ctx;  /* not owned; which job's points.csv to use */
    gm_job_metadata_t job_meta; /* loaded from job.ini on enter -- coord_sys, etc. */

    RtkFeed feed;
    RtkFeedPosition latest; /* refreshed every on_tick from feed.fn */

    MeasurePointStore points;
    bool have_origin; /* true once the first point fixes the local-fallback origin */
    double origin_lat;
    double origin_lon;

    /* Typed-field buffers. name/code back WIDGET_TEXT_FIELD widgets
     * directly (same "the widget's buf IS the field's storage"
     * convention every other text field in this codebase already
     * uses); height_buf is a plain string parsed via atof() at capture
     * time (see MEASURE_POINTS_HEIGHT_BUF_MAX's doc comment for why a
     * text buffer rather than a live double here). */
    char name_buf[GM_MEASURE_POINT_NAME_MAX];
    char code_buf[GM_MEASURE_POINT_CODE_MAX];
    char height_buf[MEASURE_POINTS_HEIGHT_BUF_MAX];
    size_t name_len;
    size_t code_len;
    size_t height_len;

    MeasurePointsActiveField active_field;

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
 * Layout constants shared between this screen's logic (grid widget
 * positions) and measure_points_screen_draw.c (panel/divider/keyboard
 * boundary rendering). Literal values rather than TFT_WIDTH/TFT_HEIGHT
 * arithmetic, so this header has no ui/tft/display.h dependency -- same
 * convention ui/core/keyboard.h's own KEYBOARD_HEIGHT already
 * established (see that header's comment: the value is documented as
 * derived from TFT_HEIGHT, not computed from it at the preprocessor
 * level here).
 *
 *   STATUS_PANEL_W/_X : right third of TFT_WIDTH (800) is the status/
 *                       input panel; everything left of it (full height
 *                       down to KEYBOARD_TOP_Y) is the map panel.
 *   PANEL_TOP_Y/_BOTTOM_Y : vertical extent the map panel and the
 *                       status/input column share -- PANEL_BOTTOM_Y is
 *                       KEYBOARD_TOP_Y (248, from ui/core/keyboard.h),
 *                       not TFT_HEIGHT, since the keyboard now owns the
 *                       bottom half of the panel on this screen (it did
 *                       not in the original, keyboard-less version of
 *                       this layout).
 *
 * Row math for the status/input column (208px of vertical budget,
 * PANEL_TOP_Y=40 to PANEL_BOTTOM_Y=248): badge, three fields, Capture
 * button, and a compact live-fix readout, in that priority order
 * (explicit field-crew decision -- the live readout is the thing most
 * willing to shrink, not the inputs needed to actually shoot a point).
 * ---------------------------------------------------------------------- */

#define STATUS_PANEL_W 266 /* TFT_WIDTH (800) / 3 */
#define STATUS_PANEL_X 534 /* TFT_WIDTH (800) - STATUS_PANEL_W */
#define PANEL_TOP_Y 40
#define PANEL_BOTTOM_Y 248 /* KEYBOARD_TOP_Y, ui/core/keyboard.h */

#define MP_FIELD_MARGIN 8
#define MP_FIELD_W 250 /* STATUS_PANEL_W - 2 * MP_FIELD_MARGIN */
#define MP_FIELD_H 22

#define MP_BADGE_Y 44
#define MP_BADGE_H 26
#define MP_NAME_Y 74
#define MP_CODE_Y 100
#define MP_HEIGHT_Y 126
#define MP_CAPTURE_Y 152
#define MP_CAPTURE_H 28
#define MP_READOUT_Y 186

#endif /* GEOMARK_UI_SCREENS_MEASURE_POINTS_SCREEN_H */