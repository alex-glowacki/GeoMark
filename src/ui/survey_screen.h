/**
 * @file survey_screen.h
 * @brief TFT survey UI — code picker, keyboard, capture progress, confirm.
 *
 * State machine driven by ui/client.c. The caller feeds button events and
 * GNSS packets; this module owns all drawing.
 *
 * States:
 *   SURVEY_UI_IDLE      — waiting, [Start Session] highlighted
 *   SURVEY_UI_PICKER    — scrollable code grid + [Other] button
 *   SURVEY_UI_KEYBOARD  — on-screen keyboard for code or description
 *   SURVEY_UI_CAPTURE   — averaging window progress bar
 *   SURVEY_UI_CONFIRM   — point saved summary (auto-advances after 2s)
 *   SURVEY_UI_ABORT     — fix quality dropped — user must acknowledge
 *
 * Navigation model:
 *   Up/Down/Left/Right  — move focus between items
 *   Center              — confirm / select focused item
 *   Left (at edge)      — back / cancel
 */

#ifndef GEOMARK_SURVEY_SCREEN_H
#define GEOMARK_SURVEY_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "survey/codelist.h"
#include "survey/survey.h"
#include "ui/input.h"

/* --------------------------------------------------------------------------
 * UI state
 * -------------------------------------------------------------------------- */

typedef enum {
    SURVEY_UI_IDLE = 0,
    SURVEY_UI_PICKER,
    SURVEY_UI_KEYBOARD,
    SURVEY_UI_CAPTURE,
    SURVEY_UI_CONFIRM,
    SURVEY_UI_ABORT,
} SurveyUIState;

/* What the keyboard is currently collecting. */
typedef enum {
    KB_MODE_CODE = 0, /* entering a custom point code    */
    KB_MODE_DESC,     /* entering an optional description */
} KeyboardMode;

/* --------------------------------------------------------------------------
 * Context passed to every survey_screen call
 * -------------------------------------------------------------------------- */

typedef struct {
    SurveyUIState state;
    KeyboardMode kb_mode;

    /* Code picker: index of the focused grid cell. */
    uint32_t picker_focus;  /* 0 .. (PICKER_ROWS*PICKER_COLS - 1) */
    uint32_t picker_scroll; /* row offset of top-left visible cell */

    /* Pending point data — set in picker/keyboard, consumed in capture. */
    char pending_code[SURVEY_CODE_MAX];
    char pending_desc[SURVEY_DESC_MAX];

    /* Keyboard: cursor position within the key grid.
     * kb_row ∈ {0,1,2,3}  (rows 0-2 = alpha rows, row 3 = action row)
     * kb_col ∈ {0..9}     (width of widest row) */
    uint32_t kb_row;
    uint32_t kb_col;

    /* Keyboard input buffer. */
    char kb_buf[SURVEY_DESC_MAX];
    uint32_t kb_len;

    /* Confirm screen: copy of the last saved point for display. */
    SurveyPoint last_point;

    /* Monotonic ms at which the confirm screen was shown (auto-advance). */
    uint32_t confirm_shown_ms;

    /* Reference to the loaded code list (not owned — do not free). */
    const CodeList *codelist;
} SurveyScreenCtx;

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

/**
 * Initialise the survey screen context and draw the idle screen.
 * Must be called after display_open() and screen_init().
 *
 * @param ctx       Caller-allocated context (zeroed by this call).
 * @param codelist  Loaded code list — must outlive ctx.
 */
void survey_screen_init(SurveyScreenCtx *ctx, const CodeList *codelist);

/* --------------------------------------------------------------------------
 * Input — feed one event per call from the main loop
 * -------------------------------------------------------------------------- */

/**
 * Feed a button event into the survey UI state machine.
 *
 * @param ctx      Survey screen context.
 * @param event    Button event from gpio_button_poll().
 * @param session  Active survey session (may be NULL in IDLE state).
 */
void survey_screen_input(SurveyScreenCtx *ctx, InputEvent event, SurveySession *session);

/* --------------------------------------------------------------------------
 * GNSS update — call at 1 Hz during capture
 * -------------------------------------------------------------------------- */

/**
 * Feed an incoming GNSS fix into the capture state.
 * Only active when state == SURVEY_UI_CAPTURE.
 */
void survey_screen_feed(SurveyScreenCtx *ctx, SurveySession *session, double lat, double lon,
                        double alt, uint8_t fix_quality, double hdop, uint8_t num_sats);

/* --------------------------------------------------------------------------
 * Periodic tick — call at 2 Hz from the render loop
 * -------------------------------------------------------------------------- */

/**
 * Handle time-driven transitions (e.g. auto-advance from confirm screen).
 *
 * @param ctx     Survey screen context.
 * @param now_ms  Current monotonic time in ms.
 */
void survey_screen_tick(SurveyScreenCtx *ctx, uint32_t now_ms);

#endif /* GEOMARK_SURVEY_SCREEN_H */