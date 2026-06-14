/**
 * @file survey_screen.c
 * @brief TFT survey UI implementation.
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

#define COL_BG          TFT_BLACK
#define COL_CHROME      TFT_DKGRAY
#define COL_LABEL       TFT_GRAY
#define COL_VALUE       TFT_WHITE
#define COL_TITLE       TFT_CYAN
#define COL_BTN_BG      TFT_DKGRAY
#define COL_BTN_FG      TFT_WHITE
#define COL_BTN_OTHER   TFT_BLUE
#define COL_BTN_ACTION  TFT_GREEN
#define COL_BTN_CANCEL  TFT_RED
#define COL_BTN_SKIP    TFT_GRAY
#define COL_PROGRESS_BG TFT_DKGRAY
#define COL_PROGRESS_FG TFT_GREEN
#define COL_SUCCESS     TFT_GREEN

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
#define SCROLL_W         12
#define SCROLL_H         50

#define OTHER_COL        2
#define OTHER_ROW        2

#define END_BTN_X    4
#define END_BTN_Y  268
#define END_BTN_W  140
#define END_BTN_H   46

/* --------------------------------------------------------------------------
 * Keyboard layout
 * -------------------------------------------------------------------------- */

static const char *KB_ROW0 = "QWERTYUIOP";
static const char *KB_ROW1 = "ASDFGHJKL";
static const char *KB_ROW2 = "ZXCVBNM";

#define KB_KEY_W    40
#define KB_KEY_H    46
#define KB_GAP       4
#define KB_ROW0_X    8
#define KB_ROW0_Y   80
#define KB_ROW1_X   24
#define KB_ROW1_Y  134
#define KB_ROW2_X   40
#define KB_ROW2_Y  188

#define KB_DEL_X   332
#define KB_DEL_Y   188
#define KB_DEL_W    60
#define KB_DEL_H    46

#define KB_SPACE_X    8
#define KB_SPACE_Y  248
#define KB_SPACE_W  160
#define KB_SKIP_X   176
#define KB_SKIP_Y   248
#define KB_SKIP_W   130
#define KB_CONFIRM_X 314
#define KB_CONFIRM_Y 248
#define KB_CONFIRM_W 158
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

static void draw_divider(uint16_t y)
{
    display_fill_rect(0, y, TFT_WIDTH, 2, COL_CHROME);
}

/* --------------------------------------------------------------------------
 * Idle screen
 * -------------------------------------------------------------------------- */

static void draw_idle(void)
{
    display_fill(COL_BG);
    display_draw_string(120, 100, "GeoMark Survey", COL_TITLE, COL_BG, 2);
    draw_button(140, 180, 200, 60, "Start Session",
                COL_BTN_ACTION, TFT_BLACK, 2);
}

/* --------------------------------------------------------------------------
 * Code picker screen
 * -------------------------------------------------------------------------- */

static void draw_picker(const SurveyScreenCtx *ctx)
{
    display_fill(COL_BG);

    uint32_t start = ctx->picker_scroll * PICKER_COLS;

    for (uint32_t row = 0; row < (uint32_t)PICKER_ROWS; row++) {
        for (uint32_t col = 0; col < (uint32_t)PICKER_COLS; col++) {

            if (row == OTHER_ROW && col == OTHER_COL) {
                uint16_t bx = (uint16_t)(PICKER_ORIGIN_X
                              + col * (PICKER_BTN_W + PICKER_GAP_X));
                uint16_t by = (uint16_t)(PICKER_ORIGIN_Y
                              + row * (PICKER_BTN_H + PICKER_GAP_Y));
                draw_button(bx, by, PICKER_BTN_W, PICKER_BTN_H,
                            "OTHER...", COL_BTN_OTHER, COL_BTN_FG, 2);
                continue;
            }

            uint32_t idx = start + row * PICKER_COLS + col;
            const CodeEntry *e = codelist_get(ctx->codelist, idx);

            uint16_t bx = (uint16_t)(PICKER_ORIGIN_X
                          + col * (PICKER_BTN_W + PICKER_GAP_X));
            uint16_t by = (uint16_t)(PICKER_ORIGIN_Y
                          + row * (PICKER_BTN_H + PICKER_GAP_Y));

            if (!e) {
                display_fill_rect(bx, by, PICKER_BTN_W, PICKER_BTN_H, COL_BG);
            } else {
                draw_button(bx, by, PICKER_BTN_W, PICKER_BTN_H,
                            e->code, COL_BTN_BG, COL_BTN_FG, 2);
            }
        }
    }

    uint32_t max_visible = (PICKER_ROWS * PICKER_COLS) - 1;
    if (ctx->codelist->count > max_visible) {
        display_draw_string(SCROLL_X, SCROLL_UP_Y, "^", COL_LABEL, COL_BG, 2);
        display_draw_string(SCROLL_X, SCROLL_DN_Y, "v", COL_LABEL, COL_BG, 2);
    }

    draw_button(END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H,
                "End Session", COL_BTN_CANCEL, COL_BTN_FG, 2);
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

    display_fill_rect(KB_INPUT_X, KB_INPUT_Y, KB_INPUT_W, KB_INPUT_H,
                      TFT_DKGRAY);
    display_draw_string(KB_INPUT_X + 6, KB_INPUT_Y + 16,
                        ctx->kb_buf, COL_VALUE, TFT_DKGRAY, 2);

    for (uint32_t i = 0; KB_ROW0[i]; i++) {
        char s[2] = { KB_ROW0[i], '\0' };
        uint16_t kx = (uint16_t)(KB_ROW0_X + i * (KB_KEY_W + KB_GAP));
        draw_button(kx, KB_ROW0_Y, KB_KEY_W, KB_KEY_H,
                    s, COL_BTN_BG, COL_BTN_FG, 2);
    }
    for (uint32_t i = 0; KB_ROW1[i]; i++) {
        char s[2] = { KB_ROW1[i], '\0' };
        uint16_t kx = (uint16_t)(KB_ROW1_X + i * (KB_KEY_W + KB_GAP));
        draw_button(kx, KB_ROW1_Y, KB_KEY_W, KB_KEY_H,
                    s, COL_BTN_BG, COL_BTN_FG, 2);
    }
    for (uint32_t i = 0; KB_ROW2[i]; i++) {
        char s[2] = { KB_ROW2[i], '\0' };
        uint16_t kx = (uint16_t)(KB_ROW2_X + i * (KB_KEY_W + KB_GAP));
        draw_button(kx, KB_ROW2_Y, KB_KEY_W, KB_KEY_H,
                    s, COL_BTN_BG, COL_BTN_FG, 2);
    }

    draw_button(KB_DEL_X,     KB_DEL_Y,     KB_DEL_W,     KB_DEL_H,
                "DEL",     COL_BTN_CANCEL, COL_BTN_FG, 2);
    draw_button(KB_SPACE_X,   KB_SPACE_Y,   KB_SPACE_W,   KB_ACTION_H,
                "SPACE",   COL_BTN_BG,     COL_BTN_FG, 2);
    draw_button(KB_SKIP_X,    KB_SKIP_Y,    KB_SKIP_W,    KB_ACTION_H,
                "SKIP",    COL_BTN_SKIP,   COL_BTN_FG, 2);
    draw_button(KB_CONFIRM_X, KB_CONFIRM_Y, KB_CONFIRM_W, KB_ACTION_H,
                "CONFIRM", COL_BTN_ACTION, TFT_BLACK,   2);
}

static void redraw_kb_input(const SurveyScreenCtx *ctx)
{
    display_fill_rect(KB_INPUT_X, KB_INPUT_Y, KB_INPUT_W, KB_INPUT_H,
                      TFT_DKGRAY);
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

static void update_capture_progress(int fixes_so_far)
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
}

/* --------------------------------------------------------------------------
 * Hit testing
 * -------------------------------------------------------------------------- */

static bool hit(uint16_t tx, uint16_t ty,
                uint16_t bx, uint16_t by, uint16_t bw, uint16_t bh)
{
    return tx >= bx && tx < bx + bw && ty >= by && ty < by + bh;
}

/* --------------------------------------------------------------------------
 * Keyboard touch handler
 * -------------------------------------------------------------------------- */

static char kb_hit(uint16_t tx, uint16_t ty)
{
    for (uint32_t i = 0; KB_ROW0[i]; i++) {
        uint16_t kx = (uint16_t)(KB_ROW0_X + i * (KB_KEY_W + KB_GAP));
        if (hit(tx, ty, kx, KB_ROW0_Y, KB_KEY_W, KB_KEY_H))
            return KB_ROW0[i];
    }
    for (uint32_t i = 0; KB_ROW1[i]; i++) {
        uint16_t kx = (uint16_t)(KB_ROW1_X + i * (KB_KEY_W + KB_GAP));
        if (hit(tx, ty, kx, KB_ROW1_Y, KB_KEY_W, KB_KEY_H))
            return KB_ROW1[i];
    }
    for (uint32_t i = 0; KB_ROW2[i]; i++) {
        uint16_t kx = (uint16_t)(KB_ROW2_X + i * (KB_KEY_W + KB_GAP));
        if (hit(tx, ty, kx, KB_ROW2_Y, KB_KEY_W, KB_KEY_H))
            return KB_ROW2[i];
    }
    if (hit(tx, ty, KB_DEL_X,     KB_DEL_Y,     KB_DEL_W,     KB_DEL_H))
        return '\b';
    if (hit(tx, ty, KB_SPACE_X,   KB_SPACE_Y,   KB_SPACE_W,   KB_ACTION_H))
        return ' ';
    if (hit(tx, ty, KB_SKIP_X,    KB_SKIP_Y,    KB_SKIP_W,    KB_ACTION_H))
        return '\x01';
    if (hit(tx, ty, KB_CONFIRM_X, KB_CONFIRM_Y, KB_CONFIRM_W, KB_ACTION_H))
        return '\r';
    return '\0';
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

void survey_screen_touch(SurveyScreenCtx *ctx,
                         uint16_t         tx,
                         uint16_t         ty,
                         SurveySession   *session)
{
    switch (ctx->state) {

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_IDLE:
        if (hit(tx, ty, 140, 180, 200, 60)) {
            log_info("survey_screen: session start tapped");
            ctx->state         = SURVEY_UI_PICKER;
            ctx->picker_scroll = 0;
            draw_picker(ctx);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_PICKER: {
        if (hit(tx, ty, END_BTN_X, END_BTN_Y, END_BTN_W, END_BTN_H)) {
            log_info("survey_screen: end session tapped");
            ctx->state = SURVEY_UI_IDLE;
            return;
        }

        if (hit(tx, ty, SCROLL_X, SCROLL_UP_Y, SCROLL_W + 10, SCROLL_H)) {
            if (ctx->picker_scroll > 0) {
                ctx->picker_scroll--;
                draw_picker(ctx);
            }
            break;
        }

        if (hit(tx, ty, SCROLL_X, SCROLL_DN_Y, SCROLL_W + 10, SCROLL_H)) {
            uint32_t max_row = (ctx->codelist->count + PICKER_COLS - 1)
                               / PICKER_COLS;
            if (ctx->picker_scroll + PICKER_ROWS < max_row) {
                ctx->picker_scroll++;
                draw_picker(ctx);
            }
            break;
        }

        for (uint32_t row = 0; row < (uint32_t)PICKER_ROWS; row++) {
            for (uint32_t col = 0; col < (uint32_t)PICKER_COLS; col++) {
                uint16_t bx = (uint16_t)(PICKER_ORIGIN_X
                              + col * (PICKER_BTN_W + PICKER_GAP_X));
                uint16_t by = (uint16_t)(PICKER_ORIGIN_Y
                              + row * (PICKER_BTN_H + PICKER_GAP_Y));

                if (!hit(tx, ty, bx, by, PICKER_BTN_W, PICKER_BTN_H))
                    continue;

                if (row == OTHER_ROW && col == OTHER_COL) {
                    ctx->kb_mode = KB_MODE_CODE;
                    ctx->kb_len  = 0;
                    memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
                    ctx->state   = SURVEY_UI_KEYBOARD;
                    draw_keyboard(ctx);
                    return;
                }

                uint32_t idx = ctx->picker_scroll * PICKER_COLS
                               + row * PICKER_COLS + col;
                const CodeEntry *e = codelist_get(ctx->codelist, idx);
                if (!e)
                    return;

                snprintf(ctx->pending_code, SURVEY_CODE_MAX, "%s", e->code);
                snprintf(ctx->pending_desc, SURVEY_DESC_MAX, "%s", e->desc);

                ctx->kb_mode = KB_MODE_DESC;
                ctx->kb_len  = 0;
                memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
                snprintf(ctx->kb_buf, SURVEY_DESC_MAX, "%s", e->desc);
                ctx->kb_len  = (uint32_t)strlen(ctx->kb_buf);

                ctx->state = SURVEY_UI_KEYBOARD;
                draw_keyboard(ctx);
                return;
            }
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_KEYBOARD: {
        char ch = kb_hit(tx, ty);
        if (ch == '\0')
            break;

        if (ch == '\b') {
            if (ctx->kb_len > 0) {
                ctx->kb_buf[--ctx->kb_len] = '\0';
                redraw_kb_input(ctx);
            }
        } else if (ch == ' ') {
            if (ctx->kb_mode == KB_MODE_DESC &&
                ctx->kb_len < SURVEY_DESC_MAX - 1) {
                ctx->kb_buf[ctx->kb_len++] = ' ';
                ctx->kb_buf[ctx->kb_len]   = '\0';
                redraw_kb_input(ctx);
            }
        } else if (ch == '\x01') {
            /* SKIP */
            if (ctx->kb_mode == KB_MODE_CODE) {
                ctx->state = SURVEY_UI_PICKER;
                draw_picker(ctx);
            } else {
                ctx->pending_desc[0] = '\0';
                goto start_capture;
            }
        } else if (ch == '\r') {
            /* CONFIRM */
            if (ctx->kb_mode == KB_MODE_CODE) {
                if (ctx->kb_len == 0)
                    break;
                /* kb_buf capped to SURVEY_CODE_MAX-1 in CODE mode */
                snprintf(ctx->pending_code, SURVEY_CODE_MAX, "%.*s",
                        (int)(SURVEY_CODE_MAX - 1), ctx->kb_buf);
                ctx->pending_desc[0] = '\0';
                ctx->kb_mode = KB_MODE_DESC;
                ctx->kb_len  = 0;
                memset(ctx->kb_buf, 0, sizeof(ctx->kb_buf));
                ctx->state   = SURVEY_UI_KEYBOARD;
                draw_keyboard(ctx);
            } else {
                /* kb_buf capped to SURVEY_DESC_MAX-1 in DESC mode */
                snprintf(ctx->pending_desc, SURVEY_DESC_MAX, "%s", ctx->kb_buf);
                goto start_capture;
            }
        } else {
            uint32_t max_len = (ctx->kb_mode == KB_MODE_CODE)
                               ? SURVEY_CODE_MAX - 1
                               : SURVEY_DESC_MAX - 1;
            if (ctx->kb_len < max_len) {
                ctx->kb_buf[ctx->kb_len++] =
                    (char)toupper((unsigned char)ch);
                ctx->kb_buf[ctx->kb_len] = '\0';
                redraw_kb_input(ctx);
            }
        }
        break;

    start_capture:
        if (session)
            survey_capture_begin(session,
                                 ctx->pending_code,
                                 ctx->pending_desc);
        ctx->state = SURVEY_UI_CAPTURE;
        draw_capture(ctx, 0, FIX_RTK_FIXED, 0.0, 0);
        break;
    }

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_CAPTURE:
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_CONFIRM:
        ctx->confirm_shown_ms = 0;
        ctx->state            = SURVEY_UI_PICKER;
        ctx->picker_scroll    = 0;
        draw_picker(ctx);
        break;

    /* ------------------------------------------------------------------ */
    case SURVEY_UI_ABORT:
        if (hit(tx, ty, 140, 200, 200, 60)) {
            ctx->state         = SURVEY_UI_PICKER;
            ctx->picker_scroll = 0;
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

    int result = survey_capture_feed(session,
                                     lat, lon, alt,
                                     fix_quality, hdop, num_sats);
    if (result < 0) {
        survey_capture_abort(session);
        ctx->state = SURVEY_UI_ABORT;
        draw_abort();
        return;
    }

    update_capture_progress(result);

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
    char buf[32];
    snprintf(buf, sizeof(buf), "HDOP %.1f", hdop);
    display_draw_string(160, 234, buf, COL_LABEL, COL_BG, 2);
    snprintf(buf, sizeof(buf), "SATS %02u", num_sats);
    display_draw_string(320, 234, buf, COL_LABEL, COL_BG, 2);

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
        draw_picker(ctx);
    }
}