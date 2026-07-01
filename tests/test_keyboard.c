#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ui/core/keyboard.h"
#include "../src/ui/core/widget.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_widget.c)
 * ========================================================================= */
static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)                                                     \
    do {                                                                      \
        g_tests_run++;                                                       \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg));  \
            g_tests_failed++;                                                \
        }                                                                    \
    } while (0)

/* =========================================================================
 * Fake screen ctx, exercising the "UiKeyboardTarget must be the first
 * member" contract documented in keyboard.h -- this struct deliberately
 * has its own fields *after* kb, the same shape any real screen
 * (e.g. NewProjectScreenCtx) uses.
 * ========================================================================= */

typedef struct {
    UiKeyboardTarget kb; /* must be first */
    int done_count;
    int own_button_activate_count; /* proves the screen's own widgets in
                                    * the same grid still receive this
                                    * exact pointer, just cast back to
                                    * (FakeScreenCtx *) instead */
} FakeScreenCtx;

static void fake_on_done(void *screen_ctx)
{
    FakeScreenCtx *ctx = (FakeScreenCtx *)screen_ctx;
    ctx->done_count++;
}

static void fake_own_button_activate(UiWidget *self, void *screen_ctx)
{
    (void)self;
    FakeScreenCtx *ctx = (FakeScreenCtx *)screen_ctx;
    ctx->own_button_activate_count++;
}

static void fake_screen_init(FakeScreenCtx *ctx, char *buf, size_t buf_cap,
                             size_t *len, UiWidgetGrid *grid,
                             UiKeyboardLabels *labels)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->kb.buf        = buf;
    ctx->kb.buf_cap    = buf_cap;
    ctx->kb.len         = len;
    ctx->kb.on_done     = fake_on_done;
    ctx->kb.screen_ctx  = ctx;

    ui_grid_init(grid, ctx);
}

/* =========================================================================
 * Char keys append to the target buffer
 * ========================================================================= */

static void test_char_keys_append(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    ASSERT(keyboard_add_to_grid(&grid, &labels), "All 44 keys fit in the grid");

    /* Find the 'A' key by its label and activate it directly -- exercises
     * exactly the path ui_grid_handle_event() would take for a tap or a
     * Center press on that key. */
    UiWidget *key_a = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "A") == 0) {
            key_a = &grid.widgets[i];
            break;
        }
    }
    ASSERT(key_a != NULL, "Key 'A' exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    grid.focus_idx = (int32_t)(key_a - grid.widgets);
    ui_grid_handle_event(&grid, activate);

    ASSERT(len == 1, "One char key press advances len by 1");
    ASSERT(strcmp(buf, "A") == 0, "Buffer contains 'A' after pressing the A key");
}

/* =========================================================================
 * '+' key: the new addition this session, for Measure Points' Code field
 * to type a breakline-start code (e.g. "+24RCPF") -- see
 * collector/breaklines.h. Same activation path as any other char key
 * (key_char_activate() has no special-casing per character), so this
 * test exists primarily to prove the key is actually present and wired
 * up in the real layout, not to re-test key_char_activate() itself
 * (test_char_keys_append() above already covers that machinery).
 * ========================================================================= */

static void test_plus_key_appends(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    ASSERT(keyboard_add_to_grid(&grid, &labels), "All 44 keys fit in the grid");

    UiWidget *key_plus = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "+") == 0) {
            key_plus = &grid.widgets[i];
            break;
        }
    }
    ASSERT(key_plus != NULL, "Key '+' exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    grid.focus_idx = (int32_t)(key_plus - grid.widgets);
    ui_grid_handle_event(&grid, activate);

    ASSERT(len == 1, "One '+' key press advances len by 1");
    ASSERT(strcmp(buf, "+") == 0, "Buffer contains '+' after pressing the + key");

    /* '+' followed by ordinary char keys should produce a real
     * breakline-start code string -- "+24RCPF" is the exact worked
     * example from the CADD editing standards Alex supplied (see
     * collector/breaklines.h's file-level doc comment). Re-finds each
     * key by label rather than reusing key_plus's neighbors, since key
     * order in the grid is an implementation detail this test
     * shouldn't depend on. */
    const char *rest = "24RCPF";
    for (const char *p = rest; *p; p++) {
        char label[2] = { *p, '\0' };
        UiWidget *key = NULL;
        for (uint32_t i = 0; i < grid.count; i++) {
            if (grid.widgets[i].label && strcmp(grid.widgets[i].label, label) == 0) {
                key = &grid.widgets[i];
                break;
            }
        }
        ASSERT(key != NULL, "Each character of the worked example exists as a key");
        if (!key) continue;
        grid.focus_idx = (int32_t)(key - grid.widgets);
        ui_grid_handle_event(&grid, activate);
    }

    ASSERT(strcmp(buf, "+24RCPF") == 0,
          "'+' followed by '24RCPF' produces the exact breakline-start code "
          "from the CADD editing standards worked example");
}

/* =========================================================================
 * '*' key: for Measure Points' Code field to append a free-text detail
 * suffix onto an otherwise-ordinary code (e.g. "GRV*4in thick") -- see
 * collector/breaklines.h. Same activation path as any other char key, so
 * (like test_plus_key_appends() above) this exists primarily to prove
 * the key is present and wired up in the real layout, and that it
 * combines correctly with plain letter/digit keys in one buffer.
 * ========================================================================= */

static void test_asterisk_key_appends(void)
{
    char   buf[24] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    ASSERT(keyboard_add_to_grid(&grid, &labels), "All 44 keys fit in the grid");

    UiWidget *key_star = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "*") == 0) {
            key_star = &grid.widgets[i];
            break;
        }
    }
    ASSERT(key_star != NULL, "Key '*' exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };

    /* Type "GRV" then '*' then "4IN THICK" (the keyboard only ever
     * produces upper-case letters -- see this file's own header
     * comment) -- Alex's own worked example for the detail-suffix
     * convention, "GRV*4in thick", typed exactly as the on-screen
     * keyboard would actually produce it. The space in "4IN THICK" is
     * typed via the "Space" action key (a whole-word label, not a
     * single-char one), not via this loop's usual per-character
     * lookup -- same special case test_decimal_key_produces_
     * parseable_float() doesn't need but test_space_appends_space()
     * above already established for the Space key specifically. */
    const char *typed = "GRV*4IN THICK";
    for (const char *p = typed; *p; p++) {
        UiWidget *key = NULL;
        if (*p == ' ') {
            for (uint32_t i = 0; i < grid.count; i++) {
                if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "Space") == 0) {
                    key = &grid.widgets[i];
                    break;
                }
            }
        } else {
            char label[2] = { *p, '\0' };
            for (uint32_t i = 0; i < grid.count; i++) {
                if (grid.widgets[i].label && strcmp(grid.widgets[i].label, label) == 0) {
                    key = &grid.widgets[i];
                    break;
                }
            }
        }
        ASSERT(key != NULL, "Each character of the worked example exists as a key");
        if (!key) continue;
        grid.focus_idx = (int32_t)(key - grid.widgets);
        ui_grid_handle_event(&grid, activate);
    }

    ASSERT(strcmp(buf, "GRV*4IN THICK") == 0,
          "'GRV', '*', and '4IN THICK' typed in sequence produce the exact "
          "detail-suffixed code from Alex's own worked example");
}

/* =========================================================================
 * Del removes the last character; no-op on an empty buffer
 * ========================================================================= */

static void test_del_removes_last_char(void)
{
    char   buf[16] = "AB";
    size_t len     = 2;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    keyboard_add_to_grid(&grid, &labels);

    UiWidget *del = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "Del") == 0) {
            del = &grid.widgets[i];
            break;
        }
    }
    ASSERT(del != NULL, "Del key exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    grid.focus_idx = (int32_t)(del - grid.widgets);
    ui_grid_handle_event(&grid, activate);
    ASSERT(len == 1 && strcmp(buf, "A") == 0, "Del removes the last character");

    ui_grid_handle_event(&grid, activate);
    ASSERT(len == 0 && buf[0] == '\0', "Del on a one-char buffer empties it");

    /* No-op on empty -- must not underflow len (it's size_t -- wrapping
     * to SIZE_MAX would silently corrupt every subsequent write). */
    ui_grid_handle_event(&grid, activate);
    ASSERT(len == 0, "Del on an empty buffer is a no-op, not an underflow");
}

/* =========================================================================
 * Space appends exactly one space character
 * ========================================================================= */

static void test_space_appends_space(void)
{
    char   buf[16] = "AB";
    size_t len     = 2;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    keyboard_add_to_grid(&grid, &labels);

    UiWidget *space = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "Space") == 0) {
            space = &grid.widgets[i];
            break;
        }
    }
    ASSERT(space != NULL, "Space key exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    grid.focus_idx = (int32_t)(space - grid.widgets);
    ui_grid_handle_event(&grid, activate);

    ASSERT(len == 3 && strcmp(buf, "AB ") == 0, "Space appends one space character");
}

/* =========================================================================
 * Buffer-full guard: a char key press beyond capacity is dropped, not
 * written out of bounds.
 * ========================================================================= */

static void test_buffer_full_guard(void)
{
    char   buf[3] = "AB"; /* cap 3: room for 'A','B','\0' -- already full */
    size_t len     = 2;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    keyboard_add_to_grid(&grid, &labels);

    UiWidget *key_c = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "C") == 0) {
            key_c = &grid.widgets[i];
            break;
        }
    }
    ASSERT(key_c != NULL, "Key 'C' exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    grid.focus_idx = (int32_t)(key_c - grid.widgets);
    ui_grid_handle_event(&grid, activate);

    ASSERT(len == 2 && strcmp(buf, "AB") == 0,
          "Char key press on a full buffer is dropped, buffer unchanged");
}

/* =========================================================================
 * Done fires on_done with the screen's own ctx pointer, and the screen's
 * own (non-keyboard) widgets in the same grid still work correctly --
 * the core proof that the first-member cast contract holds for both
 * directions simultaneously.
 * ========================================================================= */

static void test_done_and_own_widget_share_grid(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);

    /* The screen's own button, added to the SAME grid, same screen_ctx,
     * exactly the way new_project_screen.c adds its Create button before
     * calling keyboard_add_to_grid(). */
    UiRect r = {0, 0, 100, 30};
    ui_grid_add_button(&grid, r, "Create", fake_own_button_activate);

    ASSERT(keyboard_add_to_grid(&grid, &labels), "Keyboard keys fit alongside the screen's own button");

    UiWidget *done = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, "Done") == 0) {
            done = &grid.widgets[i];
            break;
        }
    }
    ASSERT(done != NULL, "Done key exists in the grid");

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };

    grid.focus_idx = (int32_t)(done - grid.widgets);
    ui_grid_handle_event(&grid, activate);
    ASSERT(ctx.done_count == 1, "Done fires on_done exactly once");

    /* The screen's own Create button is index 0 (added before the
     * keyboard keys) -- activate it and confirm it still reaches the
     * screen's own callback correctly, proving the shared screen_ctx
     * pointer works for both widget families in the same grid. */
    grid.focus_idx = 0;
    ui_grid_handle_event(&grid, activate);
    ASSERT(ctx.own_button_activate_count == 1,
          "The screen's own button (same grid) still receives the shared screen_ctx correctly");
}

/* =========================================================================
 * Grid capacity: all 44 keys plus a few of the screen's own widgets must
 * fit under UI_GRID_MAX_WIDGETS -- originally what motivated raising the
 * cap from 24 to 50; the cap has since been raised again, to 320, for
 * Measure Points' code picker (one button per point code, see
 * survey/codelist.c's 280-entry s_defaults[]), which has ample headroom
 * left over for this test's 46 widgets.
 * ========================================================================= */

static void test_full_layout_fits_with_headroom(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);

    UiRect r1 = {0, 0, 100, 20};
    UiRect r2 = {0, 20, 100, 20};
    ui_grid_add_label(&grid, r1, "Project Name");
    ui_grid_add_text_field(&grid, r2, "Project Name", buf, sizeof(buf));

    ASSERT(keyboard_add_to_grid(&grid, &labels),
          "Label + text field + 44 keyboard keys (46 widgets) fit under the cap");
    ASSERT(grid.count == 46, "Grid holds exactly label + field + 44 keys");
}

/* =========================================================================
 * Decimal key: '.' exists, appends correctly, and combines with digit
 * keys (and the existing '-' key) to produce a real parseable decimal
 * string -- the actual fix for "can't enter a non-whole-number height"
 * on Measure Points' Target height field. atof() is the same parser
 * measure_points_screen.c's on_capture_point() uses on this buffer, so
 * this test proves the typed string is exactly what that call site
 * expects, not just that the buffer "looks right".
 * ========================================================================= */

static void test_decimal_key_produces_parseable_float(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    ASSERT(keyboard_add_to_grid(&grid, &labels), "All 44 keys fit in the grid");

    UiWidget *dot = NULL;
    for (uint32_t i = 0; i < grid.count; i++) {
        if (grid.widgets[i].label && strcmp(grid.widgets[i].label, ".") == 0) {
            dot = &grid.widgets[i];
            break;
        }
    }
    ASSERT(dot != NULL, "A '.' key exists in the grid");

    /* Type "-6.5" via the existing '-' key, two digit keys, the new '.'
     * key, and one more digit key -- exercises the negative sign, the
     * decimal point, and ordinary digits all sharing one buffer. */
    const char *typed = "-6.5";
    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    for (const char *p = typed; *p; p++) {
        UiWidget *key = NULL;
        char want[2] = { *p, '\0' };
        for (uint32_t i = 0; i < grid.count; i++) {
            if (grid.widgets[i].label && strcmp(grid.widgets[i].label, want) == 0) {
                key = &grid.widgets[i];
                break;
            }
        }
        ASSERT(key != NULL, "Each character of \"-6.5\" has a matching key");
        if (key) {
            grid.focus_idx = (int32_t)(key - grid.widgets);
            ui_grid_handle_event(&grid, activate);
        }
    }

    ASSERT(strcmp(buf, "-6.5") == 0,
          "Typing '-', '6', '.', '5' through the keyboard produces \"-6.5\"");
    ASSERT(fabs(atof(buf) - (-6.5)) < 1e-9,
          "The typed string parses via atof() to exactly -6.5, "
          "matching on_capture_point()'s own parsing convention");
}

/* =========================================================================
 * Every keyboard key must be nav_excluded -- this is the actual
 * mechanism fix for the Up/Down-alternates-into-the-keyboard bug. A
 * char key, the '.' key specifically, and all three action keys
 * (Space/Del/Done) are checked individually rather than just scanning
 * for "any key with nav_excluded == false", so a future regression that
 * only affects one row or the action row specifically still fails this
 * test with a clear, attributable assertion message.
 * ========================================================================= */

static void test_every_key_is_nav_excluded(void)
{
    char   buf[16] = {0};
    size_t len     = 0;
    FakeScreenCtx ctx;
    UiWidgetGrid grid;
    UiKeyboardLabels labels;

    fake_screen_init(&ctx, buf, sizeof(buf), &len, &grid, &labels);
    ASSERT(keyboard_add_to_grid(&grid, &labels), "All 44 keys fit in the grid");

    const char *spot_check[] = { "A", "1", ".", "-", "_", "+", "*", "Space", "Del", "Done" };
    for (size_t i = 0; i < sizeof(spot_check) / sizeof(spot_check[0]); i++) {
        UiWidget *key = NULL;
        for (uint32_t j = 0; j < grid.count; j++) {
            if (grid.widgets[j].label && strcmp(grid.widgets[j].label, spot_check[i]) == 0) {
                key = &grid.widgets[j];
                break;
            }
        }
        ASSERT(key != NULL, "Spot-checked key exists in the grid");
        if (key)
            ASSERT(key->nav_excluded,
                  "Spot-checked key is marked nav_excluded (Up/Down must never land on it)");
    }

    /* Exhaustive check too -- every single one of the 44 widgets this
     * module adds, not just the spot-checked subset above, must be
     * nav_excluded. The spot checks above give an attributable failure
     * message per key kind; this confirms there is no key anywhere in
     * the layout that was missed. */
    for (uint32_t i = 0; i < grid.count; i++) {
        ASSERT(grid.widgets[i].nav_excluded,
              "Every widget keyboard_add_to_grid() adds is nav_excluded, with no exception");
    }
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_char_keys_append();
    test_plus_key_appends();
    test_asterisk_key_appends();
    test_del_removes_last_char();
    test_space_appends_space();
    test_buffer_full_guard();
    test_done_and_own_widget_share_grid();
    test_full_layout_fits_with_headroom();
    test_decimal_key_produces_parseable_float();
    test_every_key_is_nav_excluded();

    if (g_tests_failed == 0) {
        printf("All %d keyboard tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d keyboard tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}