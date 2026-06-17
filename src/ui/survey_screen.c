/**
 * @file survey_screen.c
 * @brief TFT survey UI — button-driven navigation, no touch.
 *
 * Focus is highlighted with an inverted border.
 * Left at the edge of any screen goes back one level.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ui/survey_screen.h"
#include "ui/tft/display.h"
#include "util/log.h"
#include "util/units.h"

/* --------------------------------------------------------------------------
 * Layout constants
 * -------------------------------------------------------------------------- */

#define COL_BG           TFT_BLACK
#define COL_CHROME       TFT_DKGRAY
#define COL_LABEL        TFT_GRAY
#define COL_VALUE        TFT_WHITE
#define COL_TITLE        TFT_CYAN
#define COL_BTN_BG       TFT_DKGRAY
#define COL_BTN_FG       TFT_WHITE
#define COL_BTN_OTHER    TFT_BLUE
#define COL_BTN_ACTION   TFT_GREEN
#define COL_BTN_CANCEL   TFT_RED
#define COL_BTN_SKIP     TFT_GRAY
#define COL_PROGRESS_BG  TFT_DKGRAY
#define COL_PROGRESS_FG  TFT_GREEN
#define COL_SUCCESS      TFT_GREEN
#define COL_FOCUS_BORDER TFT_YELLOW

/* Auto-advance from confirm screen after this many ms. */
#define CONFIRM_DWELL_MS  2000u

/* --------------------------------------------------------------------------
 * Code picker grid
 * -------------------------------------------------------------------------- */

#define PICKER_COLS      3
#define PICKER_ROWS      3
#define PICKER_BTN_W   148
#define PICKER_BTN_H    58
#define PICKER_GAP_X     8
#define PICKER_GAP_Y    10
#define PICKER_ORIGIN_X  4
#define PICKER_ORIGIN_Y  8

#define SCROLL_X        464
#define SCROLL_UP_Y      30
#define SCROLL_DN_Y     230

#define OTHER_COL        2
#define OTHER_ROW        2

#define END_BTN_X    4
#define END_BTN_Y  268
#define END_BTN_W  140
#define END_BTN_H   46

/* Total focusable items in picker: grid cells + End Session button.
 * "End Session" is treated as a virtual cell at index PICKER_ROWS*PICKER_COLS. */
#define PICKER_ITEM_COUNT  (PICKER_ROWS * PICKER_COLS + 1)
#define PICKER_END_IDX     (PICKER_ROWS * PICKER_COLS)

/* --------------------------------------------------------------------------
 * Keyboard layout
 * -------------------------------------------------------------------------- */

static const char *KB_ROWS[]  = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const uint32_t KB_ROW_LENS[] = { 10, 9, 7 };
#define KB_ALPHA_ROWS  3

/* Action row items (row 3): DEL, SPACE, SKIP, CONFIRM */
#define KB_ACTION_COLS  4
#define KB_ACTION_DEL     0
#define KB_ACTION_SPACE   1
#define KB_ACTION_SKIP    2
#define KB_ACTION_CONFIRM 3
#define KB_ACTION_Y       248

#define KB_KEY_W    40
#define KB_KEY_H    46
#define KB_GAP       4
#define KB_ROW0_X    8
#define KB_ROW0_Y   80
#define KB_ROW1_X   24
#define KB_ROW1_Y  134
#define KB_ROW2_X   40
#define KB_ROW2_Y  188

#define KB_DEL_X     8
#define KB_DEL_Y   248
#define KB_DEL_W    80
#define KB_SPACE_X  96
#define KB_SPACE_Y 248
#define KB_SPACE_W 100
#define KB_SKIP_X  204
#define KB_SKIP_Y  248
#define KB_SKIP_W  100
#define KB_CONFIRM_X 312
#define KB_CONFIRM_Y 248
#define KB_CONFIRM_W 160
#define KB_ACTION_H   52

#define KB_INPUT_X    8
#define KB_INPUT_Y   16
#define KB_INPUT_W  464
#define KB_INPUT_H   52

/* --------------------------------------------------------------------------
 * Progress bar
 * -------------------------------------------------------------------------- */

#define PROG_X    60
#define PROG_Y   148
#define PROG_W   360
#define PROG_H    32

/* --------------------------------------------------------------------------
 * Internal draw helpers
 * -------------------------------------------------------------------------- */

static void draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const char *label, uint16_t bg, uint16_t fg,
                        uint8_t scale)
{
    display_fill_rect(x, y, w, h, bg);
    uint16_t text_w = (uint16_t)(strlen(label) * (TFT_FONT_W + 1) * scale);
    uint16_t text_h = (uint16_t)(TFT_FONT_H * scale);
    uint16_t tx = x + (w > text_w ? (w - text_w) / 2 : 2);
    uint16_t ty = y + (h > text_h ? (h - text_h) / 2 : 2);
    display_draw_string(tx, ty, label, fg, bg, scale);
}

static void draw_focus_border(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* 2-pixel yellow border inside the button bounds */
    display_fill_rect(x,         y,         w,  2, COL_FOCUS_BORDER);
    display_fill_rect(x,         y + h - 2, w,  2, COL_FOCUS_BORDER);
    display_fill_rect(x,         y,         2,  h, COL_FOCUS_BORDER);
    display_fill_rect(x + w - 2, y,         2,  h, COL_FOCUS_BORDER);
}

static void draw_divider(uint16_t y)
{
    display_fill_rect(0, y, TFT_WIDTH, 2, COL_CHROME);
}

/* --------------------------------------------------------------------------
 * Picker geometry helpers
 * -------------------------------------------------------------------------- */

static void picker_cell_rect(uint32_t idx,
                              uint16_t *x, uint16_t *y,
                              uint16_t *w, uint16_t *h)
{
    uint32_t row = idx / PICKER_COLS;
    uint32_t col = idx % PICKER_COLS;
    *x = (uint16_t)(PICKER_ORIGIN_X + col * (PICKER_BTN_W + PICKER_GAP_X));
    *y = (uint16_t)(PICKER_ORIGIN_Y + row * (PICKER_BTN_H + PICKER_GAP_Y));
    *w = PICKER_BTN_W;
    *h = PICKER_BTN_H;
}

/* --------------------------------------------------------------------------
 * Keyboard geometry helpers
 * -------------------------------------------------------------------------- */

static void kb_alpha_key_rect(uint32_t row, uint32_t col,
                               uint16_t *x, uint16_t *y,
                               uint16_t *w, uint16_t *h)
{
    static const uint16_t row_ox[] = { KB_ROW0_X, KB_ROW1_X, KB_ROW2_X };
    static const uint16_t row_oy[] = { KB_ROW0_Y, KB_ROW1_Y, KB_ROW2_Y };
    *x = (uint16_t)(row_ox[row] + col * (KB_KEY_W + KB_GAP));
    *y = row_oy[row];
    *w = KB_KEY_W;
    *h = KB_KEY_H;
}

static void kb_action_key_rect(uint32_t col,
                                uint16_t *x, uint16_t *y,
                                uint16_t *w, uint16_t *h)
{
    static const uint16_t ax[] = { KB_DEL_X, KB_SPACE_X, KB_SKIP_X, KB_CONFIRM_X };
    static const uint16_t aw[] = { KB_DEL_W, KB_SPACE_W, KB_SKIP_W, KB_CONFIRM_W };
    *x = ax[col];
    *y = KB_ACTION_Y;
    *w = aw[col];
    *h = KB_ACTION_H;
}

/* --------------------------------------------------------------------------
 * Idle screen
 * -------------------------------------------------------------------------- */

static void draw_idle(void)
{
    display_fill(COL_BG);
    display_draw_string(100, 100, "GeoMark Survey", COL_TITLE, COL_BG, 2);
    draw_button(140, 180, 200, 60, "Start Session",
                COL_BTN_ACTION, TFT_BLACK, 2);
    draw_focus_border(140, 180, 200, 60);
}

/* --------------------------------------------------------------------------
 * Code picker screen
 * -------------------------------------------------------------------------- */

static void draw_picker_cell(const SurveyScreenCtx *ctx,
                              uint32_t visual_idx, bool focused)
{
    uint32_t row = visual_idx / PICKER_COLS;
    uint32_t col = visual_idx % PICKER_COLS;

    uint16_t bx, by, bw, bh;
    picker_cell_rect(visual_idx, &bx, &by, &bw, &bh);

    if (row == OTHER_ROW && col == OTHER_COL) {
        draw_button(bx, by, bw, bh, "OTHER...",
                    COL_BTN_OTHER, COL_BTN_FG, 2);
    } else {
        uint32_t list_idx = ctx->picker_scroll * PICKER_COLS
                            + row * PICKER_COLS + col;
        const CodeEntry *e = codelist_get(ctx->codelist, list_idx);
        if (e)
            draw_button(bx, by, bw, bh, e->code, COL_BTN_BG, COL_BTN_FG, 2);
        else
            display_fill_rect(bx, by, bw, bh, COL_BG);
    }

    if (focused)
        draw_focus_border(bx, by, bw, bh);
}

static void draw_picker(const SurveyScreenCtx *ctx)
{
    display_fill(COL_BG);

    for (uint32_t i = 0; i < (uint32_t)(PICKER_ROWS * PICKER_COLS); i++)
        draw_picker_cell(ctx, i, i == ctx->picker_focus);

    /* End Session button */
    uint16_t end_bg = (ctx->picker_focus == PICKER_END_IDX)
                      ? COL_BTN_CANCEL : COL_BTN_CANCEL;
    draw_button(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H,
                "End Session", end_bg, COL_BTN_FG, 2);
    if (ctx->picker_focus == PICKER_END_IDX)
        draw_focus_border(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H);

    /* Scroll indicators */
    uint32_t max_visible = (PICKER_ROWS * PICKER_COLS) - 1;
    if (ctx->codelist->count > max_visible) {
        display_draw_string(SCROLL_X, SCROLL_UP_Y, "^", COL_LABEL, COL_BG, 2);
        display_draw_string(SCROLL_X, SCROLL_DN_Y, "v", COL_LABEL, COL_BG, 2);
    }
}

/* --------------------------------------------------------------------------
 * Keyboard screen
 * -------------------------------------------------------------------------- */

static void draw_keyboard(const SurveyScreenCtx *ctx)
{
    display_fill(COL_BG);

    const char *prompt = (ctx->kb_mode == KB_MODE_CODE)
                         ? "Enter Point Code:"
                         : "Enter Description (optional):";
    display_draw_string(8, 4, prompt, COL_LABEL, COL_BG, 1);

    /* Input buffer */
    display_fill_rect(KB_INPUT_X, KB_INPUT_Y, KB_INPUT_W, KB_INPUT_H, TFT_DKGRAY);
    display_draw_string(KB_INPUT_X + 6, KB_INPUT_Y + 16,
                        ctx->kb_buf, COL_VALUE, TFT_DKGRAY, 2);

    /* Alpha rows */
    for (uint32_t r = 0; r < KB_ALPHA_ROWS; r++) {
        for (uint32_t c = 0; c < KB_ROW_LENS[r]; c++) {
            char s[2] = { KB_ROWS[r][c], '\0' };
            uint16_t kx, ky, kw, kh;
            kb_alpha_key_rect(r, c, &kx, &ky, &kw, &kh);
            bool focused = (ctx->kb_row == r && ctx->kb_col == c);
            draw_button(kx, ky, kw, kh, s,
                        focused ? COL_FOCUS_BORDER : COL_BTN_BG,
                        focused ? TFT_BLACK        : COL_BTN_FG, 2);
        }
    }

    /* Action row */
    static const char *action_labels[] = { "DEL", "SPACE", "SKIP", "CONFIRM" };
    static const uint16_t action_bg[]  = {
        /* DEL */     TFT_RED,
        /* SPACE */   TFT_DKGRAY,
        /* SKIP */    TFT_GRAY,
        /* CONFIRM */ TFT_GREEN,
    };
    static const uint16_t action_fg[] = {
        TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_BLACK,
    };

    for (uint32_t c = 0; c < KB_ACTION_COLS; c++) {
        uint16_t kx, ky, kw, kh;
        kb_action_key_rect(c, &kx, &ky, &kw, &kh);
        bool focused = (ctx->kb_row == KB_ALPHA_ROWS && ctx->kb_col == c);
        draw_button(kx, ky, kw, kh, action_labels[c],
                    focused ? COL_FOCUS_BORDER : action_bg[c],
                    focused ? TFT_BLACK        : action_fg[c], 2);
    }
}

static void redraw_kb_input(const SurveyScreenCtx *ctx)
{
    display_fill_rect(KB_INPUT_X, KB_INPUT_Y, KB_INPUT_W, KB_INPUT_H, TFT_DKGRAY);
    display_draw_string(KB_INPUT_X + 6, KB_INPUT_Y + 16,
                        ctx->kb_buf, COL_VALUE, TFT_DKGRAY, 2);
}

/* --------------------------------------------------------------------------
 * Capture screen
 * -------------------------------------------------------------------------- */

static void draw_capture(const SurveyScreenCtx *ctx, int fixes_so_far,
                         uint8_t fix_quality, double hdop, uint8_t num_sats)
{
    display_fill(COL_BG);

    display_draw_string(8, 16, ctx->pending_code, COL_VALUE, COL_BG, 3);
    display_draw_string(8, 56, ctx->pending_desc, COL_LABEL, COL_BG, 2);

    draw_divider(80);

    display_draw_string(8, 100, "Capturing...", COL_LABEL, COL_BG, 2);

    display_fill_rect(PROG_X, PROG_Y, PROG_W, PROG_H, COL_PROGRESS_BG);
    if (fixes_so_far > 0 && SURVEY_CAPTURE_FIXES > 0) {
        uint16_t filled = (uint16_t)((uint32_t)PROG_W
                          * (uint32_t)fixes_so_far
                          / (uint32_t)SURVEY_CAPTURE_FIXES);
        display_fill_rect(PROG_X, PROG_Y, filled, PROG_H, COL_PROGRESS_FG);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d fixes",
             fixes_so_far, SURVEY_CAPTURE_FIXES);
    display_draw_string(PROG_X, PROG_Y + PROG_H + 8,
                        buf, COL_LABEL, COL_BG, 2);

    draw_divider(220);

    const char *fq_str;
    uint16_t    fq_col;
    switch ((gm_fix_type_t)fix_quality) {
        case FIX_RTK_FIXED: fq_str = "RTK FIXED"; fq_col = TFT_GREEN;  break;
        case FIX_RTK_FLOAT: fq_str = "RTK FLOAT"; fq_col = TFT_CYAN;   break;
        case FIX_DGPS:      fq_str = "DGPS";      fq_col = TFT_YELLOW; break;
        case FIX_SINGLE:    fq_str = "SINGLE";    fq_col = TFT_ORANGE; break;
        default:            fq_str = "NO FIX";    fq_col = TFT_RED;    break;
    }
    display_draw_string(8,   234, fq_str, fq_col,   COL_BG, 2);

    snprintf(buf, sizeof(buf), "HDOP %.1f", hdop);
    display_draw_string(160, 234, buf, COL_LABEL, COL_BG, 2);

    snprintf(buf, sizeof(buf), "SATS %02u", num_sats);
    display_draw_string(320, 234, buf, COL_LABEL, COL_BG, 2);
}

static void update_capture_progress(int fixes_so_far,
                                    uint8_t fix_quality, double hdop,
                                    uint8_t num_sats)
{
    display_fill_rect(PROG_X, PROG_Y, PROG_W, PROG_H, COL_PROGRESS_BG);
    if (fixes_so_far > 0 && SURVEY_CAPTURE_FIXES > 0) {
        uint16_t filled = (uint16_t)((uint32_t)PROG_W
                          * (uint32_t)fixes_so_far
                          / (uint32_t)SURVEY_CAPTURE_FIXES);
        display_fill_rect(PROG_X, PROG_Y, filled, PROG_H, COL_PROGRESS_FG);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d fixes",
             fixes_so_far, SURVEY_CAPTURE_FIXES);
    display_fill_rect(PROG_X, PROG_Y + PROG_H + 8, 200, 20, COL_BG);
    display_draw_string(PROG_X, PROG_Y + PROG_H + 8,
                        buf, COL_LABEL, COL_BG, 2);

    display_fill_rect(0, 222, TFT_WIDTH, 30, COL_BG);
    const char *fq_str;
    uint16_t    fq_col;
    switch ((gm_fix_type_t)fix_quality) {
        case FIX_RTK_FIXED: fq_str = "RTK FIXED"; fq_col = TFT_GREEN;  break;
        case FIX_RTK_FLOAT: fq_str = "RTK FLOAT"; fq_col = TFT_CYAN;   break;
        case FIX_DGPS:      fq_str = "DGPS";      fq_col = TFT_YELLOW; break;
        case FIX_SINGLE:    fq_str = "SINGLE";    fq_col = TFT_ORANGE; break;
        default:            fq_str = "NO FIX";    fq_col = TFT_RED;    break;
    }
    display_draw_string(8,   234, fq_str, fq_col,   COL_BG, 2);
    snprintf(buf, sizeof(buf), "HDOP %.1f", hdop);
    display_draw_string(160, 234, buf, COL_LABEL, COL_BG, 2);
    snprintf(buf, sizeof(buf), "SATS %02u", num_sats);
    display_draw_string(320, 234, buf, COL_LABEL, COL_BG, 2);
}

/* --------------------------------------------------------------------------
 * Confirm screen
 * -------------------------------------------------------------------------- */

static void draw_confirm(const SurveyPoint *pt)
{
    display_fill(COL_BG);

    display_draw_string(8, 12, "Point Saved", COL_SUCCESS, COL_BG, 3);

    char buf[64];
    snprintf(buf, sizeof(buf), "#%u  %s", pt->point_num, pt->code);
    display_draw_string(8, 60, buf, COL_VALUE, COL_BG, 2);

    if (pt->desc[0])
        display_draw_string(8, 88, pt->desc, COL_LABEL, COL_BG, 2);

    draw_divider(114);

    snprintf(buf, sizeof(buf), "%.8f %c",
             pt->lat >= 0.0 ? pt->lat : -pt->lat,
             pt->lat >= 0.0 ? 'N' : 'S');
    display_draw_string(8, 130, buf, COL_VALUE, COL_BG, 2);

    snprintf(buf, sizeof(buf), "%.8f %c",
             pt->lon >= 0.0 ? pt->lon : -pt->lon,
             pt->lon >= 0.0 ? 'E' : 'W');
    display_draw_string(8, 158, buf, COL_VALUE, COL_BG, 2);

    double alt_ft = gm_m_to_intl_ft(pt->alt);
    snprintf(buf, sizeof(buf), "%.2f ft MSL", alt_ft);
    display_draw_string(8, 186, buf, COL_VALUE, COL_BG, 2);

    draw_divider(218);

    display_draw_string(8, 232, "Returning to picker...", COL_LABEL, COL_BG, 2);
}

/* --------------------------------------------------------------------------
 * Abort screen
 * -------------------------------------------------------------------------- */

static void draw_abort(void)
{
    display_fill(COL_BG);
    display_draw_string(60,  80, "Fix Quality Dropped", TFT_RED,   COL_BG, 2);
    display_draw_string(60, 114, "Point NOT saved.",    COL_LABEL, COL_BG, 2);
    draw_button(140, 200, 200, 60, "OK", COL_BTN_BG, COL_BTN_FG, 2);
    draw_focus_border(140, 200, 200, 60);
}

/* --------------------------------------------------------------------------
 * Keyboard key activation helper
 * -------------------------------------------------------------------------- */

static void kb_activate(SurveyScreenCtx *ctx, SurveySession *session)
{
    if (ctx->kb_row < KB_ALPHA_ROWS) {
        /* Alpha key */
        uint32_t max_len = (ctx->kb_mode == KB_MODE_CODE)
                           ? SURVEY_CODE_MAX - 1
                           : SURVEY_DESC_MAX - 1;
        if (ctx->kb_len < max_len) {
            ctx->kb_buf[ctx->kb_len++] =
                (char)toupper((unsigned char)KB_ROWS[ctx->kb_row][ctx->kb_col]);
            ctx->kb_buf[ctx->kb_len] = '\0';
            redraw_kb_input(ctx);
        }
        return;
    }

    /* Action row */
    switch (ctx->kb_col) {

    case KB_ACTION_DEL:
        if (ctx->kb_len > 0) {
            ctx->kb_buf[--ctx->kb_len] = '\0';
            redraw_kb_input(ctx);
        }
        break;

    case KB_ACTION_SPACE:
        if (ctx->kb_mode == KB_MODE_DESC &&
            ctx->kb_len < SURVEY_DESC_MAX - 1) {
            ctx->kb_buf[ctx->kb_len++] = ' ';
            ctx->kb_buf[ctx->kb_len]   = '\0';
            redraw_kb_input(ctx);
        }
        break;

    case KB_ACTION_SKIP:
        if (ctx->kb_mode == KB_MODE_CODE) {
            ctx->state = SURVEY_UI_PICKER;
            draw_picker(ctx);
        } else {
            ctx->pending_desc[0] = '\0';
            goto start_capture;
        }
        break;

    case KB_ACTION_CONFIRM:
        if (ctx->kb_mode == KB_MODE_CODE) {
            if (ctx->kb_len == 0)
                break;
            snprintf(ctx->pending_code, SURVEY_CODE_MAX, "%.*s",
                     (int)(SURVEY_CODE_MAX - 1), ctx->kb_buf);
            ctx->pending_desc[0] = '\0';
            ctx->kb_mode = KB_MODE_DESC;
            ctx->kb_len  = 0;
            ctx->kb_row  = 0;
            ctx->kb_col  = 0;
            memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
            draw_keyboard(ctx);
        } else {
            snprintf(ctx->pending_desc, SURVEY_DESC_MAX, "%s", ctx->kb_buf);
            goto start_capture;
        }
        break;
    }
    return;

start_capture:
    if (session)
        survey_capture_begin(session, ctx->pending_code, ctx->pending_desc);
    ctx->state = SURVEY_UI_CAPTURE;
    draw_capture(ctx, 0, FIX_RTK_FIXED, 0.0, 0);
}

/* --------------------------------------------------------------------------
 * Keyboard focus move — wraps within each row, moves between rows
 * -------------------------------------------------------------------------- */

static uint32_t kb_row_len(uint32_t row)
{
    if (row < KB_ALPHA_ROWS) return KB_ROW_LENS[row];
    return KB_ACTION_COLS;
}

static void kb_move(SurveyScreenCtx *ctx, InputEvent ev)
{
    uint32_t old_row = ctx->kb_row;
    uint32_t old_col = ctx->kb_col;
    uint32_t total_rows = KB_ALPHA_ROWS + 1;  /* alpha + action */

    switch (ev) {
    case INPUT_BTN_UP:
        if (ctx->kb_row > 0) {
            ctx->kb_row--;
            uint32_t len = kb_row_len(ctx->kb_row);
            if (ctx->kb_col >= len) ctx->kb_col = len - 1;
        }
        break;
    case INPUT_BTN_DOWN:
        if (ctx->kb_row < total_rows - 1) {
            ctx->kb_row++;
            uint32_t len = kb_row_len(ctx->kb_row);
            if (ctx->kb_col >= len) ctx->kb_col = len - 1;
        }
        break;
    case INPUT_BTN_LEFT:
        if (ctx->kb_col > 0) {
            ctx->kb_col--;
        } else {
            /* Left at col 0 = back to picker */
            ctx->state = SURVEY_UI_PICKER;
            draw_picker(ctx);
            return;
        }
        break;
    case INPUT_BTN_RIGHT:
        if (ctx->kb_col < kb_row_len(ctx->kb_row) - 1)
            ctx->kb_col++;
        break;
    default:
        break;
    }

    if (ctx->kb_row == old_row && ctx->kb_col == old_col)
        return;

    /* Redraw only changed keys */
    if (old_row < KB_ALPHA_ROWS) {
        char s[2] = { KB_ROWS[old_row][old_col], '\0' };
        uint16_t kx, ky, kw, kh;
        kb_alpha_key_rect(old_row, old_col, &kx, &ky, &kw, &kh);
        draw_button(kx, ky, kw, kh, s, COL_BTN_BG, COL_BTN_FG, 2);
    } else {
        static const char *al[] = { "DEL", "SPACE", "SKIP", "CONFIRM" };
        static const uint16_t abg[] = { TFT_RED, TFT_DKGRAY, TFT_GRAY, TFT_GREEN };
        static const uint16_t afg[] = { TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_BLACK };
        uint16_t kx, ky, kw, kh;
        kb_action_key_rect(old_col, &kx, &ky, &kw, &kh);
        draw_button(kx, ky, kw, kh, al[old_col], abg[old_col], afg[old_col], 2);
    }

    if (ctx->kb_row < KB_ALPHA_ROWS) {
        char s[2] = { KB_ROWS[ctx->kb_row][ctx->kb_col], '\0' };
        uint16_t kx, ky, kw, kh;
        kb_alpha_key_rect(ctx->kb_row, ctx->kb_col, &kx, &ky, &kw, &kh);
        draw_button(kx, ky, kw, kh, s, COL_FOCUS_BORDER, TFT_BLACK, 2);
    } else {
        static const char *al[] = { "DEL", "SPACE", "SKIP", "CONFIRM" };
        uint16_t kx, ky, kw, kh;
        kb_action_key_rect(ctx->kb_col, &kx, &ky, &kw, &kh);
        draw_button(kx, ky, kw, kh, al[ctx->kb_col], COL_FOCUS_BORDER, TFT_BLACK, 2);
    }
}

/* --------------------------------------------------------------------------
 * Picker focus move
 * -------------------------------------------------------------------------- */

static void picker_move_focus(SurveyScreenCtx *ctx, uint32_t new_focus)
{
    uint32_t old_focus = ctx->picker_focus;
    if (new_focus == old_focus) return;

    /* Redraw old focused cell without border */
    if (old_focus == PICKER_END_IDX) {
        draw_button(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H,
                    "End Session", COL_BTN_CANCEL, COL_BTN_FG, 2);
    } else {
        draw_picker_cell(ctx, old_focus, false);
    }

    ctx->picker_focus = new_focus;

    /* Redraw new focused cell with border */
    if (new_focus == PICKER_END_IDX) {
        draw_button(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H,
                    "End Session", COL_BTN_CANCEL, COL_BTN_FG, 2);
        draw_focus_border(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H);
    } else {
        draw_picker_cell(ctx, new_focus, true);
    }
}

static void picker_nav(SurveyScreenCtx *ctx, InputEvent ev)
{
    uint32_t f = ctx->picker_focus;
    uint32_t grid_count = PICKER_ROWS * PICKER_COLS;

    switch (ev) {
    case INPUT_BTN_UP:
        if (f == PICKER_END_IDX) {
            /* From End button, go to bottom-left grid cell */
            picker_move_focus(ctx, grid_count - PICKER_COLS);
        } else if (f >= (uint32_t)PICKER_COLS) {
            picker_move_focus(ctx, f - PICKER_COLS);
        } else if (ctx->picker_scroll > 0) {
            ctx->picker_scroll--;
            draw_picker(ctx);
        }
        break;

    case INPUT_BTN_DOWN:
        if (f < grid_count - PICKER_COLS) {
            picker_move_focus(ctx, f + PICKER_COLS);
        } else if (f < grid_count) {
            /* Bottom row → End button */
            picker_move_focus(ctx, PICKER_END_IDX);
        } else {
            /* Already on End button — try scroll */
            uint32_t max_row = (ctx->codelist->count + PICKER_COLS - 1)
                                / PICKER_COLS;
            if (ctx->picker_scroll + PICKER_ROWS < max_row) {
                ctx->picker_scroll++;
                draw_picker(ctx);
            }
        }
        break;

    case INPUT_BTN_LEFT:
        if (f == PICKER_END_IDX) {
            picker_move_focus(ctx, grid_count - 1);
        } else if (f % PICKER_COLS > 0) {
            picker_move_focus(ctx, f - 1);
        }
        /* Left at left edge: no-op (could go back to status; deferred) */
        break;

    case INPUT_BTN_RIGHT:
        if (f == PICKER_END_IDX) {
            /* no-op at right edge of End button */
        } else if (f % PICKER_COLS < (uint32_t)(PICKER_COLS - 1)) {
            picker_move_focus(ctx, f + 1);
        }
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void survey_screen_init(SurveyScreenCtx *ctx, const CodeList *codelist)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->codelist = codelist;
    ctx->state    = SURVEY_UI_IDLE;
    draw_idle();
}

void survey_screen_input(SurveyScreenCtx *ctx,
                         InputEvent       event,
                         SurveySession   *session)
{
    if (event == INPUT_NONE)
        return;

    switch (ctx->state) {

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_IDLE:
        /* Any button starts the session */
        if (event == INPUT_BTN_CENTER) {
            log_info("survey_screen: start session");
            ctx->state         = SURVEY_UI_PICKER;
            ctx->picker_focus  = 0;
            ctx->picker_scroll = 0;
            draw_picker(ctx);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_PICKER:
        if (event == INPUT_BTN_CENTER) {
            if (ctx->picker_focus == PICKER_END_IDX) {
                log_info("survey_screen: end session");
                ctx->state = SURVEY_UI_IDLE;
                draw_idle();
                return;
            }

            uint32_t row = ctx->picker_focus / PICKER_COLS;
            uint32_t col = ctx->picker_focus % PICKER_COLS;

            if (row == OTHER_ROW && col == OTHER_COL) {
                /* OTHER → free-text code entry */
                ctx->kb_mode = KB_MODE_CODE;
                ctx->kb_len  = 0;
                ctx->kb_row  = 0;
                ctx->kb_col  = 0;
                memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
                ctx->state   = SURVEY_UI_KEYBOARD;
                draw_keyboard(ctx);
                return;
            }

            uint32_t list_idx = ctx->picker_scroll * PICKER_COLS
                                + ctx->picker_focus;
            const CodeEntry *e = codelist_get(ctx->codelist, list_idx);
            if (!e) return;

            snprintf(ctx->pending_code, SURVEY_CODE_MAX, "%s", e->code);
            snprintf(ctx->pending_desc, SURVEY_DESC_MAX, "%s", e->desc);

            /* Offer description edit */
            ctx->kb_mode = KB_MODE_DESC;
            ctx->kb_len  = (uint32_t)strlen(e->desc);
            ctx->kb_row  = 0;
            ctx->kb_col  = 0;
            memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
            snprintf(ctx->kb_buf, SURVEY_DESC_MAX, "%s", e->desc);
            ctx->state = SURVEY_UI_KEYBOARD;
            draw_keyboard(ctx);

        } else {
            picker_nav(ctx, event);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_KEYBOARD:
        if (event == INPUT_BTN_CENTER) {
            kb_activate(ctx, session);
        } else {
            kb_move(ctx, event);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_CAPTURE:
        /* No input accepted during capture */
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_CONFIRM:
        /* Any button skips the dwell timer */
        if (event == INPUT_BTN_CENTER) {
            ctx->confirm_shown_ms = 0;
            ctx->state            = SURVEY_UI_PICKER;
            ctx->picker_scroll    = 0;
            ctx->picker_focus     = 0;
            draw_picker(ctx);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_ABORT:
        if (event == INPUT_BTN_CENTER) {
            ctx->state         = SURVEY_UI_PICKER;
            ctx->picker_scroll = 0;
            ctx->picker_focus  = 0;
            draw_picker(ctx);
        }
        break;
    }
}

void survey_screen_feed(SurveyScreenCtx *ctx,
                        SurveySession   *session,
                        double           lat,
                        double           lon,
                        double           alt,
                        uint8_t          fix_quality,
                        double           hdop,
                        uint8_t          num_sats)
{
    if (ctx->state != SURVEY_UI_CAPTURE)
        return;

    int result = survey_capture_feed(session, lat, lon, alt,
                                     fix_quality, hdop, num_sats);
    if (result < 0) {
        survey_capture_abort(session);
        ctx->state = SURVEY_UI_ABORT;
        draw_abort();
        return;
    }

    update_capture_progress(result, fix_quality, hdop, num_sats);

    if (!survey_capture_ready(session))
        return;

    SurveyPoint pt;
    if (survey_capture_finish(session, &pt) != 0) {
        ctx->state = SURVEY_UI_ABORT;
        draw_abort();
        return;
    }

    ctx->last_point       = pt;
    ctx->confirm_shown_ms = 0;
    ctx->state            = SURVEY_UI_CONFIRM;
    draw_confirm(&pt);
}

void survey_screen_tick(SurveyScreenCtx *ctx, uint32_t now_ms)
{
    if (ctx->state != SURVEY_UI_CONFIRM)
        return;

    if (ctx->confirm_shown_ms == 0)
        ctx->confirm_shown_ms = now_ms;

    if (now_ms - ctx->confirm_shown_ms >= CONFIRM_DWELL_MS) {
        ctx->confirm_shown_ms = 0;
        ctx->state            = SURVEY_UI_PICKER;
        ctx->picker_scroll    = 0;
        ctx->picker_focus     = 0;
        draw_picker(ctx);
    }
}