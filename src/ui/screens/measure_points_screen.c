/**
 * @file ui/screens/measure_points_screen.c
 * @brief Measure Points screen logic. No ui/tft/display.h dependency
 *        (other than through ui/core/keyboard.h's KEYBOARD_TOP_Y/HEIGHT,
 *        plain constants, not function calls -- same convention
 *        job_create_screen.c already uses) -- stays unit-testable on
 *        host.
 *
 * Grid rebuild model: rebuild_grid() is the single source of truth for
 * what's currently focusable. It is called from init, on_enter, and
 * every show_keyboard()/hide_keyboard()/show_code_picker()/
 * hide_code_picker(). There is exactly one grid; its contents change
 * based on ctx->overlay, never two grids or a visibility flag
 * ui_grid_move_focus() would need to understand (no such mechanism
 * exists in widget.h) -- see measure_points_screen.h's file-level doc
 * comment for the full rationale.
 */

#define _GNU_SOURCE

#include "ui/screens/measure_points_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util/log.h"
#include "util/units.h"

/* -------------------------------------------------------------------------
 * Layout constants shared with measure_points_screen_draw.c live in
 * measure_points_screen.h (STATUS_PANEL_W/_X, PANEL_TOP_Y/_BOTTOM_Y,
 * MP_*, MP_OVERLAY_*) -- see that header for why.
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * RTK feed
 * ---------------------------------------------------------------------- */

static void no_feed_fn(void *user, RtkFeedPosition *out)
{
    (void)user;
    memset(out, 0, sizeof(*out));
    out->valid = false;
}

RtkFeed measure_points_no_feed(void)
{
    RtkFeed f = { .fn = no_feed_fn, .user = NULL };
    return f;
}

/* -------------------------------------------------------------------------
 * Forward declarations -- rebuild_grid() and the overlay show/hide
 * functions are mutually referential (activating a field shows the
 * keyboard, which calls rebuild_grid(), which re-adds the field
 * widgets whose on_activate callbacks call back into the show/hide
 * functions).
 * ---------------------------------------------------------------------- */

static void rebuild_grid(MeasurePointsScreenCtx *ctx);
static void show_keyboard(MeasurePointsScreenCtx *ctx);
static void hide_keyboard(MeasurePointsScreenCtx *ctx);

/* -------------------------------------------------------------------------
 * Keyboard field switching -- same pattern job_create_screen.c's
 * set_active_field() already established for its six fields, here for
 * this screen's three (Point name, Code, Target height). Activating
 * any of the three also shows the keyboard automatically -- see this
 * header's file-level doc comment on the dual show triggers (auto on
 * field activation, explicit via the toggle button).
 * ---------------------------------------------------------------------- */

static void set_active_field(MeasurePointsScreenCtx *ctx, MeasurePointsActiveField field)
{
    ctx->active_field = field;

    switch (field) {
    case MEASURE_POINTS_FIELD_NAME:
        ctx->kb.buf     = ctx->name_buf;
        ctx->kb.buf_cap = sizeof(ctx->name_buf);
        ctx->kb.len     = &ctx->name_len;
        break;
    case MEASURE_POINTS_FIELD_CODE:
        ctx->kb.buf     = ctx->code_buf;
        ctx->kb.buf_cap = sizeof(ctx->code_buf);
        ctx->kb.len     = &ctx->code_len;
        break;
    case MEASURE_POINTS_FIELD_HEIGHT:
        ctx->kb.buf     = ctx->height_buf;
        ctx->kb.buf_cap = sizeof(ctx->height_buf);
        ctx->kb.len     = &ctx->height_len;
        break;
    case MEASURE_POINTS_FIELD_NONE:
    default:
        ctx->kb.buf = NULL;
        ctx->kb.len = NULL;
        break;
    }
}

static void on_name_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    set_active_field(ctx, MEASURE_POINTS_FIELD_NAME);
    show_keyboard(ctx);
}
static void on_code_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    set_active_field(ctx, MEASURE_POINTS_FIELD_CODE);
    show_keyboard(ctx);
}
static void on_height_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    set_active_field(ctx, MEASURE_POINTS_FIELD_HEIGHT);
    show_keyboard(ctx);
}

static void on_keyboard_done(void *screen_ctx)
{
    /* Unlike job_create_screen.c's Done (which just drops keyboard
     * focus but leaves the keyboard rendered), this screen's Done
     * actually hides the overlay entirely -- the keyboard isn't
     * permanently part of the layout here, so "done typing" means
     * "put it away," not just "stop editing this field." */
    hide_keyboard((MeasurePointsScreenCtx *)screen_ctx);
}

/* -------------------------------------------------------------------------
 * Keyboard toggle button (explicit show/hide trigger, in addition to
 * the automatic show-on-field-activate above).
 * ---------------------------------------------------------------------- */

static void on_keyboard_toggle(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    if (ctx->overlay == MEASURE_POINTS_OVERLAY_KEYBOARD)
        hide_keyboard(ctx);
    else
        show_keyboard(ctx);
}

/* -------------------------------------------------------------------------
 * Code picker
 *
 * Built directly from the already-loaded ctx->codelist (codelist_load()
 * is called once, in measure_points_screen_init() -- see this file's
 * own header comment on why reusing this read-only data is safe).
 * One button per code entry, same "list of buttons in a scrollable
 * region" pattern open_job_screen.c's rebuild_job_list() already
 * established for its own dynamic widget set -- though unlike that
 * screen's list (which can exceed the visible area and needs a scroll
 * region), CODELIST_MAX_ENTRIES (128) combined with the overlay's own
 * height means this may need scrolling too for a fully-populated list;
 * the scroll region is set up the same way regardless of how many
 * entries actually exist, so it costs nothing when the list is short.
 * ---------------------------------------------------------------------- */

static void on_code_picked(UiWidget *self, void *screen_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    /* self->label points directly at a CodeEntry's own code[] buffer
     * (see add_code_picker_buttons() below) -- copy it out before the
     * grid is rebuilt and that widget (and its label pointer) stop
     * being valid. */
    strncpy(ctx->code_buf, self->label, sizeof(ctx->code_buf) - 1);
    ctx->code_buf[sizeof(ctx->code_buf) - 1] = '\0';
    ctx->code_len = strlen(ctx->code_buf);

    ctx->overlay = MEASURE_POINTS_OVERLAY_NONE;
    rebuild_grid(ctx);
}

static void on_pick_code_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    ctx->overlay = MEASURE_POINTS_OVERLAY_CODE_PICKER;
    rebuild_grid(ctx);
}

#define MP_CODE_PICKER_ROW_H   28
#define MP_CODE_PICKER_ROW_GAP  4
#define MP_CODE_PICKER_MARGIN  12

static void add_code_picker_buttons(MeasurePointsScreenCtx *ctx)
{
    uint16_t y = MP_OVERLAY_TOP_Y + 4;
    uint16_t w = (uint16_t)(800 - 2 * MP_CODE_PICKER_MARGIN); /* TFT_WIDTH literal,
                                                                * same convention
                                                                * keyboard.h itself
                                                                * uses for its own
                                                                * row math (no
                                                                * display.h dep) */

    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, MP_OVERLAY_TOP_Y, 800, MP_OVERLAY_HEIGHT});

    for (uint32_t i = 0; i < ctx->codelist.count; i++) {
        const CodeEntry *entry = codelist_get(&ctx->codelist, i);
        if (!entry)
            break;

        UiRect r = { MP_CODE_PICKER_MARGIN, y, w, MP_CODE_PICKER_ROW_H };
        /* entry->code is owned by ctx->codelist (loaded once at init,
         * outlives every widget built from it) -- same "caller-owned
         * label that outlives the widget" convention widget.h's
         * file-level doc comment requires, not a transient buffer. */
        UiWidget *btn = ui_grid_add_button(&ctx->grid, r, entry->code, on_code_picked);
        ui_widget_mark_scrollable(btn);
        y = (uint16_t)(y + MP_CODE_PICKER_ROW_H + MP_CODE_PICKER_ROW_GAP);
    }
}

/* -------------------------------------------------------------------------
 * Overlay show/hide -- single entry points; both are idempotent (hiding
 * an already-hidden overlay, or showing an already-shown one, is a
 * no-op aside from the redundant rebuild) and mutually exclusive
 * (showing one always clears the other first).
 * ---------------------------------------------------------------------- */

static void show_keyboard(MeasurePointsScreenCtx *ctx)
{
    ctx->overlay = MEASURE_POINTS_OVERLAY_KEYBOARD;
    rebuild_grid(ctx);
}

static void hide_keyboard(MeasurePointsScreenCtx *ctx)
{
    /* Idempotent by construction: if overlay is already NONE or is the
     * code picker (not the keyboard), this still safely lands on NONE
     * -- there is no path here that could ever re-show a hidden
     * keyboard, satisfying the "hide is a one-way action, never a
     * toggle" requirement this function's callers (Done, the toggle
     * button when already showing) depend on. */
    if (ctx->overlay == MEASURE_POINTS_OVERLAY_KEYBOARD)
        ctx->overlay = MEASURE_POINTS_OVERLAY_NONE;
    rebuild_grid(ctx);
}

/* -------------------------------------------------------------------------
 * Point-name auto-increment
 *
 * "Purely numeric" means strtol() consumes the ENTIRE string (endptr
 * lands exactly on the trailing NUL) and the string is non-empty --
 * the standard idiom for "is this whole string an integer", not just
 * "does it start with one" (strtol() alone would happily accept
 * "12abc" and silently ignore "abc", which is not what "purely
 * numeric" should mean here).
 * ---------------------------------------------------------------------- */

static bool parse_purely_numeric(const char *s, long *out)
{
    if (!s || s[0] == '\0')
        return false;

    char *endptr = NULL;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || *endptr != '\0')
        return false;

    *out = v;
    return true;
}

static void advance_name_if_numeric(MeasurePointsScreenCtx *ctx)
{
    long v;
    if (!parse_purely_numeric(ctx->name_buf, &v))
        return; /* non-numeric name -- left untouched, see header doc comment */

    snprintf(ctx->name_buf, sizeof(ctx->name_buf), "%ld", v + 1);
    ctx->name_len = strlen(ctx->name_buf);
}

/* -------------------------------------------------------------------------
 * Capture
 *
 * Only reachable when ctx->overlay == MEASURE_POINTS_OVERLAY_NONE --
 * the Capture Point button is not added to the grid at all otherwise
 * (see rebuild_grid() below), so this callback firing already implies
 * the keyboard/code-picker were hidden first. No additional "is an
 * overlay open" guard is needed here as a result.
 * ---------------------------------------------------------------------- */

static void on_capture_point(UiWidget *self, void *screen_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    (void)self;

    if (!ctx->latest.valid) {
        ctx->status = MEASURE_POINTS_STATUS_NO_FIX;
        return;
    }

    /* Target height: typed in feet (see measure_points_screen.h's
     * file-level doc comment for why), parsed via atof() (same
     * string-to-double convention gnss/nmea.c already uses), converted
     * to meters for internal storage. An empty/unparseable height_buf
     * yields 0.0 ft -- atof()'s own documented behavior on a string
     * with no valid number, which is exactly "no correction applied",
     * a sane default for a field crew that hasn't entered one yet. */
    double target_height_ft = atof(ctx->height_buf);
    double target_height_m  = gm_intl_ft_to_m(target_height_ft);

    MeasurePoint pt;
    memset(&pt, 0, sizeof(pt));
    pt.lat             = ctx->latest.lat;
    pt.lon             = ctx->latest.lon;
    pt.raw_alt         = ctx->latest.alt;
    pt.target_height_m = target_height_m;
    pt.alt             = pt.raw_alt - pt.target_height_m; /* corrected ground elevation */
    pt.fix_quality     = ctx->latest.fix_quality;
    pt.hdop            = ctx->latest.hdop;
    pt.num_sats        = ctx->latest.num_sats;
    pt.timestamp       = time(NULL);
    snprintf(pt.name, sizeof(pt.name), "%.*s", (int)sizeof(pt.name) - 1, ctx->name_buf);
    snprintf(pt.code, sizeof(pt.code), "%.*s", (int)sizeof(pt.code) - 1, ctx->code_buf);

    gm_status_t rc = measure_points_add(&ctx->points, pt);
    if (rc != GM_OK) {
        ctx->status = MEASURE_POINTS_STATUS_STORE_FULL;
        return;
    }

    /* The just-added copy has point_num assigned by measure_points_add();
     * re-read it from the store rather than the local pt, which still
     * has point_num == 0. */
    const MeasurePoint *stored = &ctx->points.points[ctx->points.count - 1];

    if (!ctx->have_origin) {
        ctx->origin_lat  = stored->lat;
        ctx->origin_lon  = stored->lon;
        ctx->have_origin = true;
    }

    if (ctx->job_ctx && ctx->job_ctx->job_dir[0] != '\0') {
        char csv_path[600];
        measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
        measure_points_append_csv(csv_path, stored);
    }

    advance_name_if_numeric(ctx);

    ctx->status = MEASURE_POINTS_STATUS_NONE;
    log_info("measure_points: captured point #%u ('%s')", stored->point_num, stored->name);
}

static void on_export_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    ui_stack_push(ctx->stack, ctx->export_screen);
}

/* -------------------------------------------------------------------------
 * Point list overlay
 *
 * Shows every captured point as a scrollable list covering the full
 * panel width (same MP_OVERLAY_TOP_Y / MP_OVERLAY_HEIGHT region as the
 * keyboard overlay). Each row shows "# <name>  <code>" on the left and
 * a "Del" button on the right. Tapping Del removes that point from the
 * in-memory store, rewrites points.csv on disk, and rebuilds the
 * overlay in place so the list reflects the deletion immediately. The
 * mini-map reads directly from ctx->points so the deleted dot
 * disappears on the next render frame with no additional wiring.
 *
 * Widget identification for the delete callback: UiWidget carries only
 * a const char* label (widget.h), no per-widget user-data field. To
 * identify WHICH row's Del button was pressed inside on_delete_point(),
 * each Del button's label is set to the corresponding entry of
 * mp_list_del_labels[] -- a static per-row char array whose ADDRESS is
 * unique per row. on_delete_point() scans mp_list_del_labels[i] pointer
 * addresses to find i, then reads mp_list_delete_indices[i] for the
 * actual store index. Both arrays are rebuilt on every overlay open /
 * post-deletion rebuild, same "full rebuild, not incremental patch"
 * pattern rebuild_grid() already uses for all other widget sets.
 * ---------------------------------------------------------------------- */

#define MP_LIST_ROW_H   30  /* height per row in the point list overlay */
#define MP_LIST_GAP      3  /* vertical gap between rows */
#define MP_LIST_MARGIN   8  /* left margin inside the overlay */
#define MP_LIST_DEL_W   52  /* width of each "Del" button */
#define MP_LIST_LABEL_W (800 - 2 * MP_LIST_MARGIN - MP_LIST_DEL_W - 6)

/** Per-row display label text ("# <name>  <code>"), written into a
 *  static buffer whose address doubles as the identification key for
 *  the corresponding Del button (see on_delete_point()). */
static char mp_list_row_labels[GM_MEASURE_POINTS_MAX][48];

/** Per-row "Del" button label. Each entry holds the text "Del" with a
 *  unique buffer ADDRESS per row -- on_delete_point() matches
 *  self->label against mp_list_del_labels[i] addresses to recover i. */
static char mp_list_del_labels[GM_MEASURE_POINTS_MAX][4]; /* "Del\0" */

/** Parallel: store index corresponding to each Del button's row.
 *  After a deletion the store shifts, so this is rebuilt on every
 *  add_point_list_buttons() call. */
static uint32_t mp_list_delete_indices[GM_MEASURE_POINTS_MAX];

static void on_delete_point(UiWidget *self, void *screen_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;

    /* Identify which row was pressed by matching self->label (which
     * points at one of the mp_list_del_labels[i] buffers) against each
     * buffer's address -- unique per row by construction. */
    uint32_t store_idx = ctx->points.count; /* sentinel: "not found" */
    for (uint32_t i = 0; i < ctx->points.count; i++) {
        if (self->label == mp_list_del_labels[i]) {
            store_idx = mp_list_delete_indices[i];
            break;
        }
    }

    if (store_idx >= ctx->points.count) {
        log_warn("on_delete_point: could not match Del button to a store index -- ignoring");
        return;
    }

    if (measure_points_remove(&ctx->points, store_idx) != GM_OK)
        return;

    /* Update origin if the deleted point was the origin (index 0 before
     * removal -- after removal index 0 is what was index 1, etc.). */
    if (ctx->points.count == 0) {
        ctx->have_origin = false;
    } else if (store_idx == 0) {
        ctx->origin_lat = ctx->points.points[0].lat;
        ctx->origin_lon = ctx->points.points[0].lon;
    }

    /* Rewrite points.csv to keep disk and memory in sync. */
    if (ctx->job_ctx && ctx->job_ctx->job_dir[0] != '\0') {
        char csv_path[600];
        measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
        measure_points_rewrite_csv(csv_path, &ctx->points);
    }

    /* Rebuild the overlay in place so the list reflects the deletion. */
    rebuild_grid(ctx);
}

static void add_point_list_buttons(MeasurePointsScreenCtx *ctx)
{
    ui_grid_set_scroll_region(&ctx->grid,
        (UiRect){0, MP_OVERLAY_TOP_Y, 800, MP_OVERLAY_HEIGHT});

    /* Start rows below the title banner drawn in measure_points_screen_
     * _draw.c (title at MP_OVERLAY_TOP_Y+6, separator at +22, so
     * first row starts at +28 to clear both). */
    uint16_t y = (uint16_t)(MP_OVERLAY_TOP_Y + 28);

    for (uint32_t i = 0; i < ctx->points.count; i++) {
        const MeasurePoint *pt = &ctx->points.points[i];

        /* Row display label: "#N  <name>  <code>", fits in 48 chars.
         * Cap name/code at 15 each: "#" + up to 10 digit point_num +
         * "  " + 15 + "  " + 15 + NUL = 46 bytes max, fits buffer. */
        snprintf(mp_list_row_labels[i], sizeof(mp_list_row_labels[i]),
                 "#%u  %.15s  %.15s",
                 pt->point_num, pt->name, pt->code);

        /* Del button label: "Del" written per-row so each has a unique
         * buffer address on_delete_point() can use for identification. */
        snprintf(mp_list_del_labels[i], sizeof(mp_list_del_labels[i]), "Del");
        mp_list_delete_indices[i] = i;

        UiRect label_r = { (uint16_t)MP_LIST_MARGIN, y,
                           (uint16_t)MP_LIST_LABEL_W, (uint16_t)MP_LIST_ROW_H };
        UiWidget *lbl = ui_grid_add_button(&ctx->grid, label_r,
                                           mp_list_row_labels[i], NULL);
        if (lbl) ui_widget_mark_scrollable(lbl);

        uint16_t del_x = (uint16_t)(MP_LIST_MARGIN + MP_LIST_LABEL_W + 6);
        UiRect del_r = { del_x, y,
                         (uint16_t)MP_LIST_DEL_W, (uint16_t)MP_LIST_ROW_H };
        UiWidget *del = ui_grid_add_button(&ctx->grid, del_r,
                                           mp_list_del_labels[i], on_delete_point);
        if (del) ui_widget_mark_scrollable(del);

        y = (uint16_t)(y + MP_LIST_ROW_H + MP_LIST_GAP);
    }
}

static void on_points_list_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    ctx->overlay = MEASURE_POINTS_OVERLAY_POINT_LIST;
    rebuild_grid(ctx);
}

/**
 * See ui/core/widget.h's ui_grid_add_back_button() doc comment.
 * Dispatching UI_EVENT_BACK here (rather than popping directly) means a
 * tap on this button gets exactly the same handling as a real BACK event
 * from any other input source -- in particular, measure_points_on_event()
 * below closes an open keyboard/code-picker overlay first and only pops
 * the stack on a second BACK once no overlay is open. No special-casing
 * needed here for overlay state.
 */
static void on_back(UiWidget *self, void *screen_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)screen_ctx;
    (void)self;
    ui_stack_dispatch_event(ctx->stack, (UiEvent){ .type = UI_EVENT_BACK });
}

/* -------------------------------------------------------------------------
 * Grid rebuild -- single source of truth for the currently focusable
 * widget set, driven entirely by ctx->overlay. Called from init,
 * on_enter, and every overlay show/hide. See this file's top-of-file
 * doc comment for the full rationale.
 * ---------------------------------------------------------------------- */

static void add_base_widgets(MeasurePointsScreenCtx *ctx)
{
    UiRect name_r   = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_NAME_Y,   MP_NAME_W, MP_FIELD_H };
    UiRect code_r   = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_CODE_Y,   MP_CODE_W, MP_FIELD_H };
    UiRect pick_r   = { MP_PICK_CODE_X, MP_CODE_Y, MP_PICK_CODE_W, MP_FIELD_H };
    UiRect height_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_HEIGHT_Y, MP_HEIGHT_W, MP_FIELD_H };
    UiRect kb_toggle_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_KEYBOARD_TOGGLE_Y,
                          MP_NAME_W, MP_KEYBOARD_TOGGLE_H };

    UiWidget *name_w = ui_grid_add_text_field(&ctx->grid, name_r, "Point name",
                                              ctx->name_buf, sizeof(ctx->name_buf));
    if (name_w) name_w->on_activate = on_name_activate;

    UiWidget *code_w = ui_grid_add_text_field(&ctx->grid, code_r, "Code",
                                              ctx->code_buf, sizeof(ctx->code_buf));
    if (code_w) code_w->on_activate = on_code_activate;

    ui_grid_add_button(&ctx->grid, pick_r, "Pick", on_pick_code_activate);

    UiWidget *height_w = ui_grid_add_text_field(&ctx->grid, height_r, "Target height",
                                                ctx->height_buf, sizeof(ctx->height_buf));
    if (height_w) height_w->on_activate = on_height_activate;

    ui_grid_add_button(&ctx->grid, kb_toggle_r, "Keyboard", on_keyboard_toggle);

    /* Capture Point is intentionally NOT added here -- see this file's
     * header comment on why it only exists in the grid when no overlay
     * is open. Added by rebuild_grid() itself, conditionally. */
}

/* "Points" button layout -- occupies the same slot in add_base_widgets'
 * geometry but is rendered separately from the Capture Point / Export
 * buttons (those are only added in OVERLAY_NONE mode by rebuild_grid).
 * Placed after Keyboard toggle so it is still present when no overlay
 * is open; not added when an overlay is open (same convention as Export
 * and Capture Point -- the base widgets with activation callbacks that
 * could conflict with an open overlay are not in the grid). */

static void rebuild_grid(MeasurePointsScreenCtx *ctx)
{
    /* The grid has no "remove all widgets" operation (widget.h has no
     * such function -- grids are append-only by design, see
     * UI_GRID_MAX_WIDGETS's fixed-capacity model), so this
     * re-initializes the whole grid via ui_grid_init() before re-adding
     * the current overlay's widget set -- same pattern
     * open_job_screen.c's rebuild_job_list() already established. */
    ui_grid_init(&ctx->grid, ctx);

    switch (ctx->overlay) {
    case MEASURE_POINTS_OVERLAY_KEYBOARD:
        add_base_widgets(ctx);
        /* Added after add_base_widgets() so ui_grid_focus_first() still
         * lands on Point name (the first focusable widget), not here --
         * same focus-order reasoning as new_project_screen.c's own
         * back-button placement comment. */
        ui_grid_add_back_button(&ctx->grid, on_back);
        keyboard_add_to_grid(&ctx->grid, &ctx->kb_labels);
        break;

    case MEASURE_POINTS_OVERLAY_CODE_PICKER:
        add_base_widgets(ctx);
        ui_grid_add_back_button(&ctx->grid, on_back);
        add_code_picker_buttons(ctx);
        break;

    case MEASURE_POINTS_OVERLAY_POINT_LIST:
        /* Back button only -- the base fields are intentionally NOT
         * added here (same reasoning as KEYBOARD/CODE_PICKER: the
         * overlay owns the full focusable set while open). */
        ui_grid_add_back_button(&ctx->grid, on_back);
        add_point_list_buttons(ctx);
        break;

    case MEASURE_POINTS_OVERLAY_NONE:
    default: {
        add_base_widgets(ctx);
        ui_grid_add_back_button(&ctx->grid, on_back);
        UiRect capture_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_CAPTURE_Y,
                             MP_NAME_W, MP_CAPTURE_H };
        ui_grid_add_button(&ctx->grid, capture_r, "Capture Point", on_capture_point);

        UiRect export_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_EXPORT_Y,
                            MP_NAME_W, MP_EXPORT_H };
        ui_grid_add_button(&ctx->grid, export_r, "Export", on_export_activate);

        UiRect pts_r = { STATUS_PANEL_X + MP_FIELD_MARGIN, MP_POINTS_LIST_Y,
                         MP_NAME_W, MP_POINTS_LIST_H };
        ui_grid_add_button(&ctx->grid, pts_r, "Points", on_points_list_activate);
        break;
    }
    }
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void measure_points_screen_init(MeasurePointsScreenCtx *ctx, UiScreenStack *stack,
                                const JobContext *job_ctx, RtkFeed feed, UiScreen export_screen)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack         = stack;
    ctx->job_ctx       = job_ctx;
    ctx->feed          = feed;
    ctx->export_screen = export_screen;

    job_metadata_defaults(&ctx->job_meta);
    measure_points_init(&ctx->points);
    codelist_load(&ctx->codelist); /* always succeeds -- falls back to built-in
                                    * defaults if no point_codes.txt is found,
                                    * see codelist.h's own doc comment */

    /* Point names start at "1" -- the first shot of a brand-new screen
     * instance is purely-numeric by default, so auto-increment applies
     * immediately without the crew having to type anything first. */
    strncpy(ctx->name_buf, "1", sizeof(ctx->name_buf) - 1);
    ctx->name_len = strlen(ctx->name_buf);

    ctx->kb.on_done    = on_keyboard_done;
    ctx->kb.screen_ctx = ctx;

    ctx->overlay = MEASURE_POINTS_OVERLAY_NONE;
    ui_grid_init(&ctx->grid, ctx);
    rebuild_grid(ctx);

    /* Point name is the first focusable widget added -- start editing
     * it by default, same convention every keyboard-using screen in
     * this codebase already follows (job_create_screen.c, etc.). Does
     * NOT call show_keyboard() -- the keyboard starts hidden on this
     * screen (unlike job_create_screen.c's always-visible keyboard),
     * consistent with "the field crew sees the full map/status layout
     * first, the keyboard only appears once they actually start
     * typing." */
    set_active_field(ctx, MEASURE_POINTS_FIELD_NAME);
}

/**
 * Reloads this job's metadata and previously-captured points from disk.
 * Called from on_enter (not init) -- same "read the real on-disk state
 * every time this screen becomes visible, not just once at construction"
 * stance open_job_screen.c's rebuild_job_list() already established,
 * since the active job (via JobContext) can change between visits to
 * this screen (a different job opened via Open Existing Job, etc.).
 *
 * Deliberately does NOT touch name_buf/code_buf/height_buf -- those are
 * live field-crew input, not job-derived state; re-entering this screen
 * for a different job should not silently erase whatever was mid-typed
 * (the field crew's actual workflow is shoot-shoot-shoot within one
 * job, rarely re-entering this screen for a *different* job with
 * pending unsaved field text).
 */
static void reload_job_data(MeasurePointsScreenCtx *ctx)
{
    ctx->status      = MEASURE_POINTS_STATUS_NONE;
    ctx->have_origin = false;
    measure_points_init(&ctx->points);
    job_metadata_defaults(&ctx->job_meta);

    if (!ctx->job_ctx || !job_context_has_job(ctx->job_ctx)) {
        ctx->status = MEASURE_POINTS_STATUS_NO_JOB;
        return;
    }

    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", ctx->job_ctx->job_dir);
    job_metadata_load(ini_path, &ctx->job_meta); /* missing file -> defaults, not an error */

    char csv_path[600];
    measure_points_csv_path(ctx->job_ctx->job_dir, csv_path, sizeof(csv_path));
    gm_status_t rc = measure_points_load_csv(csv_path, &ctx->points);
    if (rc != GM_OK) {
        ctx->status = MEASURE_POINTS_STATUS_LOAD_ERROR;
        return;
    }

    if (ctx->points.count > 0) {
        ctx->origin_lat  = ctx->points.points[0].lat;
        ctx->origin_lon  = ctx->points.points[0].lon;
        ctx->have_origin = true;
    }
}

static void measure_points_on_enter(void *raw_ctx)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;
    reload_job_data(ctx);
    /* Re-entering the screen always starts with both overlays hidden,
     * regardless of what was showing the last time this screen was
     * active -- the keyboard/code-picker are momentary editing aids,
     * not state worth preserving across a navigation round-trip. */
    ctx->overlay = MEASURE_POINTS_OVERLAY_NONE;
    rebuild_grid(ctx);
    ui_grid_focus_first(&ctx->grid);
}

static void measure_points_on_tick(void *raw_ctx, uint32_t now_ms)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;
    (void)now_ms;

    if (ctx->feed.fn)
        ctx->feed.fn(ctx->feed.user, &ctx->latest);

    if (ctx->latest.valid && !ctx->have_origin) {
        ctx->origin_lat  = ctx->latest.lat;
        ctx->origin_lon  = ctx->latest.lon;
        ctx->have_origin = true;
    }
}

static bool measure_points_on_event(void *raw_ctx, UiEvent ev)
{
    MeasurePointsScreenCtx *ctx = (MeasurePointsScreenCtx *)raw_ctx;

    if (ev.type == UI_EVENT_BACK) {
        /* If an overlay is open, BACK closes it instead of leaving the
         * screen -- same "BACK has a local meaning before it falls
         * through to the stack" precedent as e.g. a discard-changes
         * prompt would use (see screen_stack.h's own doc comment on
         * UI_EVENT_BACK), just simpler: there's nothing to confirm,
         * just close the overlay. Covers all three overlay types
         * (KEYBOARD, CODE_PICKER, POINT_LIST) uniformly. */
        if (ctx->overlay != MEASURE_POINTS_OVERLAY_NONE) {
            ctx->overlay = MEASURE_POINTS_OVERLAY_NONE;
            rebuild_grid(ctx);
            return true;
        }
        return false; /* unconsumed -- stack pops back to Job Create/Open Job */
    }

    return ui_grid_handle_event(&ctx->grid, ev);
}

UiScreen measure_points_screen_as_ui_screen(MeasurePointsScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter  = measure_points_on_enter;
    s.on_event  = measure_points_on_event;
    s.on_tick   = measure_points_on_tick;
    s.on_render = measure_points_screen_render;
    s.ctx       = ctx;
    return s;
}