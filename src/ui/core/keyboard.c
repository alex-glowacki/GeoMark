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
 * Combined length must equal KB_CHAR_KEY_COUNT (38): 10+10+9+9.
 * ---------------------------------------------------------------------- */

static const char *const KB_ROW0 = "1234567890";
static const char *const KB_ROW1 = "QWERTYUIOP";
static const char *const KB_ROW2 = "ASDFGHJKL";
static const char *const KB_ROW3 = "ZXCVBNM-_";

#define KB_KEY_W   40
#define KB_KEY_H   46
#define KB_GAP      4

#define KB_ROW0_Y (KEYBOARD_TOP_Y + 0 * (KB_KEY_H + KB_GAP))
#define KB_ROW1_Y (KEYBOARD_TOP_Y + 1 * (KB_KEY_H + KB_GAP))
#define KB_ROW2_Y (KEYBOARD_TOP_Y + 2 * (KB_KEY_H + KB_GAP))
#define KB_ROW3_Y (KEYBOARD_TOP_Y + 3 * (KB_KEY_H + KB_GAP))
#define KB_ACTION_Y (KEYBOARD_TOP_Y + 4 * (KB_KEY_H + KB_GAP))
#define KB_ACTION_H KB_KEY_H

/* Each alpha row is horizontally offset slightly to mimic a real
 * staggered QWERTY layout, same visual convention as the legacy keyboard. */
#define KB_ROW0_X 8
#define KB_ROW1_X 8
#define KB_ROW2_X 24
#define KB_ROW3_X 40

#define KB_SPACE_X   8
#define KB_SPACE_W 220
#define KB_DEL_X   236
#define KB_DEL_W   140
#define KB_DONE_X  384
#define KB_DONE_W  140

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
        if (!ui_grid_add_button(grid, r, label_slot, key_char_activate))
            return false;
        x = (uint16_t)(x + KB_KEY_W + KB_GAP);
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

    ok = ok && (ui_grid_add_button(grid, space_r, "Space", key_space_activate) != NULL);
    ok = ok && (ui_grid_add_button(grid, del_r,   "Del",   key_del_activate)   != NULL);
    ok = ok && (ui_grid_add_button(grid, done_r,  "Done",  key_done_activate)  != NULL);

    if (!ok)
        log_error("ui/keyboard: grid ran out of room while adding keys");

    return ok;
}