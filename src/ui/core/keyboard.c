/**
 * @file ui/core/keyboard.c
 * @brief On-screen QWERTY keyboard logic. See keyboard.h for the design
 *        rationale (touch-first, shared grid, closed character set, the
 *        first-member UiKeyboardTarget cast convention, and why label
 *        storage is caller-owned rather than module-static).
 *
 * No ui/tft/display.h dependency — keys are plain WIDGET_BUTTON entries,
 * so rendering is already handled by the existing ui_grid_render() /
 * widget_draw.c path with zero changes there. This keeps keyboard.c
 * unit-testable on host with no display hardware, same convention as
 * widget.c itself.
 */

#define _GNU_SOURCE

#include "ui/core/keyboard.h"

#include "util/log.h"

/* -------------------------------------------------------------------------
 * Row contents — letters stored uppercase to match the legacy
 * survey_screen.c keyboard's on-screen convention (typed text displays
 * uppercase regardless of key label case, since there is only one case).
 * Combined length must equal KB_CHAR_KEY_COUNT (39): 10+10+10+9.
 *
 * '.' lives at the end of Row 2 (ASDFGHJKL.) rather than getting a row of
 * its own: a fifth char row would overflow KEYBOARD_HEIGHT's fixed
 * 5-row budget (4 char rows + action row, see KB_KEY_H's doc comment
 * below), and appending it to Row 2 conveniently brings that row up to
 * 10 keys -- the same count as Rows 0/1 -- so all three rows share one
 * KB_KEY_W (see the full-width sizing comment below) with no extra
 * layout case to add. Added for decimal numeric entry (e.g. Measure
 * Points' Target height field, "6.562") -- see keyboard.h's file-level
 * doc comment for the character-set-safety reasoning.
 * ---------------------------------------------------------------------- */

static const char *const KB_ROW0 = "1234567890";
static const char *const KB_ROW1 = "QWERTYUIOP";
static const char *const KB_ROW2 = "ASDFGHJKL.";
static const char *const KB_ROW3 = "ZXCVBNM-_";

/* KB_KEY_H/KB_GAP_V sized so 5 rows (4 char rows + action row) fit
 * exactly within KEYBOARD_HEIGHT (232px): 5*43 + 4*3 = 227px, leaving a
 * 5px margin at the very bottom of the panel -- unchanged by the
 * full-width rework below, since adding '.' to Row 2 added a column,
 * not a row. 43px still comfortably exceeds typical minimum touch-target
 * guidance (~40px) for a capacitive panel this size.
 *
 * KB_KEY_W/KB_GAP_H: full-width rework (real-hardware feedback: the
 * original 40px-wide, left-justified keys only spanned ~440px of the
 * 800px panel, leaving the right third empty and looking lopsided/
 * cramped). One uniform key width for every char row, sized so the
 * three 10-key rows (0/1/2) span 776px (10*74 + 9*4 = 776), centered
 * with a 12px margin each side -- this is the widest row, so it sets
 * the shared key size. Row 3 (9 keys, one fewer) is centered
 * independently at the SAME key width, producing a wider 51px inset
 * each side -- this is what preserves the staggered-QWERTY look (Row 3
 * already reads as "indented" relative to the rows above it, same as
 * the original layout's intent), just filling the full panel width
 * instead of stopping at ~440px. */
#define KB_KEY_W     74
#define KB_KEY_H     43
#define KB_GAP_H      4
#define KB_GAP_V      3

#define KB_ROW0_Y (KEYBOARD_TOP_Y + 0 * (KB_KEY_H + KB_GAP_V))
#define KB_ROW1_Y (KEYBOARD_TOP_Y + 1 * (KB_KEY_H + KB_GAP_V))
#define KB_ROW2_Y (KEYBOARD_TOP_Y + 2 * (KB_KEY_H + KB_GAP_V))
#define KB_ROW3_Y (KEYBOARD_TOP_Y + 3 * (KB_KEY_H + KB_GAP_V))
#define KB_ACTION_Y (KEYBOARD_TOP_Y + 4 * (KB_KEY_H + KB_GAP_V))
#define KB_ACTION_H KB_KEY_H

/* Row X offsets -- each row centered independently within the 800px
 * panel at the shared KB_KEY_W above (see that constant's doc comment
 * for the centering math). TFT_WIDTH literal (800), not a #include of
 * ui/tft/display.h -- same no-display-dependency convention this
 * module's own file-level doc comment already establishes, and the same
 * literal-800 convention measure_points_screen.c's own
 * add_code_picker_buttons() already uses for an identical reason. */
#define KB_ROW0_X 12  /* 10 keys: (800 - (10*74 + 9*4)) / 2 = 12 */
#define KB_ROW1_X 12  /* 10 keys, same span as Row 0 */
#define KB_ROW2_X 12  /* 10 keys (incl '.'), same span as Row 0/1 */
#define KB_ROW3_X 51  /* 9 keys: (800 - (9*74 + 8*4)) / 2 = 51 */

/* Action row (Space/Del/Done) -- same 12px margin and full-width span as
 * the char rows above, rather than the original layout's left-justified
 * Space/Del/Done that left the right ~270px of the row empty. Space
 * gets half the available width (a single most-used key deserves the
 * largest target); Del and Done split the remaining half evenly. */
#define KB_SPACE_X   12
#define KB_SPACE_W  384  /* (800 - 2*12 - 2*4) / 2 */
#define KB_DEL_X    400  /* KB_SPACE_X + KB_SPACE_W + KB_GAP_H */
#define KB_DEL_W    192  /* remaining width / 2 */
#define KB_DONE_X   596  /* KB_DEL_X + KB_DEL_W + KB_GAP_H */
#define KB_DONE_W   192

/* -------------------------------------------------------------------------
 * Key activation
 *
 * Every on_activate below receives screen_ctx exactly as
 * ui_grid_handle_event() -> activate_widget() passes it: grid->screen_ctx,
 * grid-wide, identical for every widget in the grid (the screen's own
 * Create button included). This module's contract (see keyboard.h) is
 * that whatever ctx struct the owning screen used as grid->screen_ctx
 * has a UiKeyboardTarget as its first member, which is exactly what
 * makes this cast valid.
 * ---------------------------------------------------------------------- */

static void key_char_activate(UiWidget *self, void *screen_ctx)
{
    UiKeyboardTarget *t = (UiKeyboardTarget *)screen_ctx;
    if (!t->buf || !t->len) return;
    if (!self->label || self->label[0] == '\0') return;

    if (*t->len + 1 >= t->buf_cap) {
        log_warn("ui/keyboard: '%s' dropped -- text field is full (%zu/%zu)",
                 self->label, *t->len, t->buf_cap);
        return;
    }

    t->buf[*t->len]     = self->label[0];
    t->buf[*t->len + 1] = '\0';
    (*t->len)++;
}

static void key_space_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    UiKeyboardTarget *t = (UiKeyboardTarget *)screen_ctx;
    if (!t->buf || !t->len) return;

    if (*t->len + 1 >= t->buf_cap) {
        log_warn("ui/keyboard: space dropped -- text field is full (%zu/%zu)",
                 *t->len, t->buf_cap);
        return;
    }

    t->buf[*t->len]     = ' ';
    t->buf[*t->len + 1] = '\0';
    (*t->len)++;
}

static void key_del_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    UiKeyboardTarget *t = (UiKeyboardTarget *)screen_ctx;
    if (!t->buf || !t->len) return;
    if (*t->len == 0) return;

    (*t->len)--;
    t->buf[*t->len] = '\0';
}

static void key_done_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    UiKeyboardTarget *t = (UiKeyboardTarget *)screen_ctx;
    if (t->on_done)
        t->on_done(t->screen_ctx);
}

/* -------------------------------------------------------------------------
 * Construction
 * ---------------------------------------------------------------------- */

static bool add_char_row(UiWidgetGrid *grid, const char *row,
                         uint16_t row_x, uint16_t row_y,
                         UiKeyboardLabels *labels, uint32_t *slot)
{
    uint16_t x = row_x;
    for (const char *p = row; *p; p++) {
        if (*slot >= KB_CHAR_KEY_COUNT) {
            log_error("ui/keyboard: label storage exhausted at '%c' "
                     "(KB_CHAR_KEY_COUNT=%d too small for actual row lengths)",
                     *p, KB_CHAR_KEY_COUNT);
            return false;
        }
        char *label_slot = labels->slots[*slot];
        label_slot[0] = *p;
        label_slot[1] = '\0';
        (*slot)++;

        UiRect r = { x, row_y, KB_KEY_W, KB_KEY_H };
        UiWidget *key = ui_grid_add_button(grid, r, label_slot, key_char_activate);
        if (!key) return false;
        /* Up/Down nav buttons must never land on a keyboard key -- see
         * UiWidget::nav_excluded's doc comment (widget.h) for the
         * alternating-focus bug this prevents on any screen that
         * combines this keyboard with nav buttons in the same grid
         * (Job Create today; Measure Points once nav buttons are added
         * there for its own longer field list). Tap is unaffected. */
        ui_widget_mark_nav_excluded(key);
        x = (uint16_t)(x + KB_KEY_W + KB_GAP_H);
    }
    return true;
}

bool keyboard_add_to_grid(UiWidgetGrid *grid, UiKeyboardLabels *labels)
{
    uint32_t slot = 0;
    bool ok = true;
    ok = ok && add_char_row(grid, KB_ROW0, KB_ROW0_X, KB_ROW0_Y, labels, &slot);
    ok = ok && add_char_row(grid, KB_ROW1, KB_ROW1_X, KB_ROW1_Y, labels, &slot);
    ok = ok && add_char_row(grid, KB_ROW2, KB_ROW2_X, KB_ROW2_Y, labels, &slot);
    ok = ok && add_char_row(grid, KB_ROW3, KB_ROW3_X, KB_ROW3_Y, labels, &slot);

    UiRect space_r = { KB_SPACE_X, KB_ACTION_Y, KB_SPACE_W, KB_ACTION_H };
    UiRect del_r   = { KB_DEL_X,   KB_ACTION_Y, KB_DEL_W,   KB_ACTION_H };
    UiRect done_r  = { KB_DONE_X,  KB_ACTION_Y, KB_DONE_W,  KB_ACTION_H };

    UiWidget *space_key = ui_grid_add_button(grid, space_r, "Space", key_space_activate);
    UiWidget *del_key   = ui_grid_add_button(grid, del_r,   "Del",   key_del_activate);
    UiWidget *done_key  = ui_grid_add_button(grid, done_r,  "Done",  key_done_activate);

    /* Same nav_excluded reasoning as the char keys above -- the action
     * row is just as reachable by Up/Down's geometric search as any
     * char key would be, so it needs the same exclusion. */
    ui_widget_mark_nav_excluded(space_key);
    ui_widget_mark_nav_excluded(del_key);
    ui_widget_mark_nav_excluded(done_key);

    ok = ok && (space_key != NULL) && (del_key != NULL) && (done_key != NULL);

    if (!ok)
        log_error("ui/keyboard: grid ran out of room while adding keys");

    return ok;
}