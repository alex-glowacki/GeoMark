/**
 * @file ui/screens/measure_points_screen.h
 * @brief Measure Points — the screen Job Create and Open Existing Job
 *        both push into. Layout: left 2/3 a live map panel plotting
 *        captured points (white background, black markers/text), right
 *        1/3 a fix badge, Point name / Code / Target height fields
 *        (each with a title label, same convention job_create_screen.h
 *        established), the Capture Point action, and a compact live-fix
 *        readout. This layout is FIXED at full panel height (40 to 472)
 *        at all times -- unlike job_create_screen.h/new_project_screen.h,
 *        which permanently reserve the bottom KEYBOARD_HEIGHT pixels for
 *        an always-visible keyboard, Measure Points needs its map and
 *        readout to never shrink to make room for one (real-time RTK
 *        status and the map are the primary view; typing is occasional).
 *
 * Keyboard and code-picker are OVERLAYS, not part of the fixed layout:
 * both render on top of the bottom portion of the existing screen,
 * covering whatever was drawn there, and are shown/hidden rather than
 * permanently occupying space. Only one overlay is visible at a time
 * (showing one hides the other). See MeasurePointsOverlay below.
 *
 *   - Keyboard shows automatically when Point name, Code, or Target
 *     height is activated; can also be toggled directly via a small
 *     dedicated button; hides via its own Done key or the same toggle
 *     button. While the keyboard (or the code picker) is shown, it
 *     replaces the screen's own focusable widgets in the grid entirely
 *     -- see the grid-rebuild paragraph below -- which means Capture
 *     Point is simply not present to be pressed until an overlay is
 *     explicitly closed. This is a deliberate field-safety measure:
 *     the keyboard must be hidden (Done, or the toggle) before Capture
 *     Point is reachable at all, so a shot can never be taken with the
 *     keyboard still covering the screen.
 *   - Code picker shows when the Code field's "Pick Code" button is
 *     pressed; tapping an entry in the list fills the Code field and
 *     hides the picker. Built on survey/codelist.h's existing CodeList/
 *     codelist_load() -- read-only reuse of the same point_codes.txt
 *     list the legacy ui/client.c flow already loads (see this header's
 *     own doc comment on why reusing read-only shared data, as opposed
 *     to SurveyPoint/SurveySession's session *behavior*, is safe; no
 *     coupling to the legacy capture flow's lifecycle is introduced).
 *     The Code field remains a free-typed WIDGET_TEXT_FIELD regardless
 *     -- picking a code is optional, never required.
 *
 * Because the overlay and the screen's own fields can't both be
 * focusable at once (the d-pad's Up/Down/Center has no way to express
 * "navigate within the overlay only"), the grid is rebuilt (ui_grid_init()
 * + re-add) every time overlay visibility changes -- same "tear down and
 * re-add the current widget set" pattern open_job_screen.c's
 * rebuild_job_list() already established for its own dynamic widget set.
 * Hidden overlay widgets are therefore never in the grid to begin with,
 * not merely skipped by some visibility flag ui_grid_move_focus() would
 * otherwise need to understand (no such mechanism exists in widget.h).
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
 * on_capture_point() in the .c file). Target height in particular has
 * to be correct *before* the shot, since it directly corrects the
 * measured elevation (see measure_points.h's MeasurePoint doc comment).
 *
 * Point name auto-increment: if, at the moment Capture succeeds, the
 * Point name field's content parses entirely as a non-negative integer
 * (e.g. "1", "42") -- checked with the same strtol()-and-check-the-
 * endptr technique as any other "is this string purely numeric"
 * check -- the field is advanced to that integer + 1 for the next shot
 * ("1" -> "2"). Any non-purely-numeric name (e.g. "BENCHMARK_A") is
 * left untouched, since there's no sane "next" value for it.
 *
 * Target height unit: entered and displayed in feet (international
 * foot, matching units.h's existing convention that vertical
 * measurements use the international foot, not the US survey foot --
 * see units.h's own file-level doc comment), converted to meters via
 * gm_intl_ft_to_m() before being stored in MeasurePoint::target_height_m
 * (which is meters, matching this codebase's all-internal-SI
 * convention). Persists across captures by design (typed once, stays
 * until changed) -- a field crew's rod height rarely changes during a
 * session.
 *
 * Navigation note: this screen is touch-only -- the physical GPIO d-pad
 * is no longer read by ui/preview.c's input loop (see ui/preview.h's
 * controls doc; the prior Right-button-unreliable/Left-mapped-to-BACK
 * d-pad design is gone, not just worked around). The standard top-left
 * "< Back" button (ui/core/widget.h's ui_grid_add_back_button()) is the
 * only way back, dispatching the same UI_EVENT_BACK that closes an open
 * keyboard/code-picker overlay first -- see measure_points_on_event()'s
 * own BACK handling, which the back button reuses rather than
 * duplicating. The keyboard's own no-Left/Right-within-a-row limitation
 * (see ui/core/keyboard.h's file-level doc comment) is unaffected by
 * this -- tap is still the only way to move within a key row, same as
 * before. The map panel itself is a live display, not a focus target.
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
#include "survey/codelist.h"
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

/**
 * Which overlay (if any) currently owns the bottom portion of the
 * screen and the grid's focusable widget set. NONE means the screen's
 * own three fields + Pick Code + Capture Point + keyboard-toggle button
 * are what's in the grid; KEYBOARD/CODE_PICKER mean that overlay's
 * widgets replace them until hidden. Mutually exclusive by construction
 * (showing one always hides the other first, see show_keyboard()/
 * show_code_picker() in the .c file) -- there is no UI for both at once.
 */
typedef enum {
    MEASURE_POINTS_OVERLAY_NONE = 0,
    MEASURE_POINTS_OVERLAY_KEYBOARD,
    MEASURE_POINTS_OVERLAY_CODE_PICKER,
} MeasurePointsOverlay;

/** Upper bound on code-picker list entries shown per screen -- matches
 *  CODELIST_MAX_ENTRIES (survey/codelist.h), since the picker can never
 *  show more entries than the underlying CodeList holds regardless. */
#define MEASURE_POINTS_CODE_PICKER_MAX CODELIST_MAX_ENTRIES

typedef struct {
    /* MUST be first -- see ui/core/keyboard.h's file-level doc comment. */
    UiKeyboardTarget kb;

    UiWidgetGrid grid;
    UiKeyboardLabels kb_labels;

    UiScreenStack *stack;       /* not owned */
    UiScreen export_screen;     /* pushed by the Export button -- see
                                 * export_screen.h for why this is a
                                 * separate pushed screen rather than
                                 * widgets added directly here */
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
    MeasurePointsOverlay overlay;

    /** Loaded once at init via codelist_load() -- read-only point-code
     *  list shared with the legacy survey flow's own loader (see this
     *  header's file-level doc comment on why this reuse is safe). */
    CodeList codelist;

    MeasurePointsStatus status;
} MeasurePointsScreenCtx;

/**
 * job_ctx is not owned and must outlive this screen -- supplies the
 * active job's resolved directory (see job_context.h). feed is copied
 * by value (it is itself just a function pointer + opaque pointer, see
 * RtkFeed above) -- pass measure_points_no_feed() until real hardware
 * wiring exists. export_screen is pushed by the Export button -- same
 * caller-supplied-destination pattern job_create_screen_init()/
 * open_job_screen_init() already use for the measure_points_screen
 * they're each given (see export_screen.h for what this screen is).
 */
void measure_points_screen_init(MeasurePointsScreenCtx *ctx, UiScreenStack *stack,
                                const JobContext *job_ctx, RtkFeed feed, UiScreen export_screen);

/** Render implementation — measure_points_screen_draw.c (depends on ui/tft/display.h). */
void measure_points_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen measure_points_screen_as_ui_screen(MeasurePointsScreenCtx *ctx);

/* -------------------------------------------------------------------------
 * Layout constants shared between this screen's logic (grid widget
 * positions) and measure_points_screen_draw.c (panel/divider/overlay
 * boundary rendering). Literal values rather than TFT_WIDTH/TFT_HEIGHT
 * arithmetic, so this header has no ui/tft/display.h dependency -- same
 * convention ui/core/keyboard.h's own KEYBOARD_HEIGHT already
 * established.
 *
 *   STATUS_PANEL_W/_X : right third of TFT_WIDTH (800) is the status/
 *                       input panel; everything left of it is the map
 *                       panel. Both span the FULL panel height
 *                       (PANEL_TOP_Y to PANEL_BOTTOM_Y) at all times --
 *                       unlike the prior keyboard-reserves-space layout,
 *                       this no longer depends on KEYBOARD_TOP_Y.
 *   OVERLAY_TOP_Y/_HEIGHT : the region the keyboard/code-picker overlay
 *                       covers when shown -- bottom third of the panel,
 *                       drawn ON TOP of whatever the fixed layout
 *                       already rendered there, not a layout region the
 *                       fixed content avoids.
 *
 * Row math for the status/input column (PANEL_TOP_Y=40 to
 * PANEL_BOTTOM_Y=472, 432px of vertical budget -- this is now the FULL
 * original budget the keyboard used to eat into, not 208px): badge,
 * three labeled fields with title labels above each (matching
 * job_create_screen.h's label-above-field convention), Pick Code button
 * beside the Code field, a keyboard-toggle button, Capture Point, and a
 * live-fix readout, in that explicit top-to-bottom order. Capture Point
 * sits BELOW the keyboard toggle, not above it, by design: per this
 * screen's own UX decision, the keyboard overlay must be explicitly
 * hidden (its own Done key or the toggle button) before Capture Point
 * is reachable at all -- both the keyboard and the code picker are
 * removed from the grid's focusable set while shown (see
 * MeasurePointsOverlay's doc comment), so Capture Point is simply not
 * present to be pressed until an overlay is closed. This replaces an
 * earlier "Capture auto-hides the keyboard" design that would have
 * needed Capture to sit ABOVE the overlay region (y < 248) to remain
 * reachable while the keyboard was open -- moot now, since Capture is
 * never in the grid at the same time as the keyboard regardless of its
 * Y position.
 * ---------------------------------------------------------------------- */

#define STATUS_PANEL_W 266 /* TFT_WIDTH (800) / 3 */
#define STATUS_PANEL_X 534 /* TFT_WIDTH (800) - STATUS_PANEL_W */
#define PANEL_TOP_Y 40
#define PANEL_BOTTOM_Y 472 /* TFT_HEIGHT (480) - 8 */

#define MP_FIELD_MARGIN 8
#define MP_LABEL_H 14
#define MP_FIELD_H 24

#define MP_BADGE_Y 44
#define MP_BADGE_H 28

#define MP_NAME_LABEL_Y 82
#define MP_NAME_Y (MP_NAME_LABEL_Y + MP_LABEL_H)
#define MP_NAME_W 250 /* STATUS_PANEL_W - 2*MP_FIELD_MARGIN */

#define MP_CODE_LABEL_Y (MP_NAME_Y + MP_FIELD_H + 6)
#define MP_CODE_Y (MP_CODE_LABEL_Y + MP_LABEL_H)
#define MP_CODE_W 168 /* leaves room for the Pick Code button beside it */
#define MP_PICK_CODE_X (STATUS_PANEL_X + MP_FIELD_MARGIN + MP_CODE_W + 6)
#define MP_PICK_CODE_W 76 /* MP_NAME_W - MP_CODE_W - 6 */

#define MP_HEIGHT_LABEL_Y (MP_CODE_Y + MP_FIELD_H + 6)
#define MP_HEIGHT_Y (MP_HEIGHT_LABEL_Y + MP_LABEL_H)
#define MP_HEIGHT_W 160 /* leaves room for the ft unit label beside it */

#define MP_KEYBOARD_TOGGLE_Y (MP_HEIGHT_Y + MP_FIELD_H + 10)
#define MP_KEYBOARD_TOGGLE_H 26

#define MP_CAPTURE_Y (MP_KEYBOARD_TOGGLE_Y + MP_KEYBOARD_TOGGLE_H + 10)
#define MP_CAPTURE_H 32

#define MP_READOUT_Y (MP_CAPTURE_Y + MP_CAPTURE_H + 10)

/* Export button -- below the live-fix readout's three text rows
 * (MP_READOUT_Y..MP_READOUT_Y+~36px), inside the ~130px of panel
 * height that remains free below it (PANEL_BOTTOM_Y=472 minus the
 * readout's own bottom edge) -- this screen's fixed layout has room
 * for exactly one more row here without needing any redesign. Pushes
 * export_screen (see export_screen.h) rather than performing the
 * export inline -- this screen's right panel has no space left for
 * a result message or format choice beyond a single button, see
 * that header's own doc comment for the full reasoning. */
#define MP_EXPORT_Y (MP_READOUT_Y + 48)
#define MP_EXPORT_H 32

/* -------------------------------------------------------------------------
 * Overlay region -- matches ui/core/keyboard.h's own fixed footprint
 * exactly (KEYBOARD_TOP_Y=248, KEYBOARD_HEIGHT=232) rather than a
 * custom shorter region, since keyboard_add_to_grid() has no parameter
 * to render at a different position/height -- it always lays its rows
 * out starting at KEYBOARD_TOP_Y (see that header/source). Using the
 * same footprint here means the keyboard module itself needs zero
 * changes; the code picker (built fresh for this screen, not from a
 * shared module) deliberately reuses the identical region so the two
 * overlays look consistent and share one set of draw-time boundary
 * constants. Covers the bottom half of the panel ON TOP of whatever
 * the fixed map/status layout already drew there -- the fixed layout
 * itself never shrinks to make room.
 * ---------------------------------------------------------------------- */

#define MP_OVERLAY_TOP_Y 248  /* KEYBOARD_TOP_Y, ui/core/keyboard.h */
#define MP_OVERLAY_HEIGHT 232 /* KEYBOARD_HEIGHT, ui/core/keyboard.h */

#endif /* GEOMARK_UI_SCREENS_MEASURE_POINTS_SCREEN_H */