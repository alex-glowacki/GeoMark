#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/ui/core/screen_stack.h"
#include "../src/ui/core/widget.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_collector.c)
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
 * Widget grid: construction, focus navigation, hit-testing
 * ========================================================================= */

typedef struct {
    int activate_count;
    UiWidget *last_widget;
} ActivateProbe;

static void probe_activate(UiWidget *self, void *screen_ctx)
{
    ActivateProbe *p = (ActivateProbe *)screen_ctx;
    p->activate_count++;
    p->last_widget = self;
}

static void test_grid_focus_navigation(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);

    /* A label is never focusable — must not interfere with focus_first. */
    ui_grid_add_label(&grid, (UiRect){0, 0, 100, 20}, "Title");

    /* 2x2 button grid: TL, TR, BL, BR */
    UiWidget *tl = ui_grid_add_button(&grid, (UiRect){0,   40, 50, 50}, "TL", probe_activate);
    UiWidget *tr = ui_grid_add_button(&grid, (UiRect){60,  40, 50, 50}, "TR", probe_activate);
    UiWidget *bl = ui_grid_add_button(&grid, (UiRect){0,  100, 50, 50}, "BL", probe_activate);
    UiWidget *br = ui_grid_add_button(&grid, (UiRect){60, 100, 50, 50}, "BR", probe_activate);

    ASSERT(tl && tr && bl && br, "All four buttons allocated");

    ASSERT(ui_grid_focus_first(&grid), "focus_first finds a focusable widget");
    ASSERT(grid.focus_idx >= 0 && &grid.widgets[grid.focus_idx] == tl,
           "focus_first lands on TL, not the label");

    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_RIGHT), "TL -> right moves");
    ASSERT(&grid.widgets[grid.focus_idx] == tr, "TL -> right lands on TR");

    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN), "TR -> down moves");
    ASSERT(&grid.widgets[grid.focus_idx] == br, "TR -> down lands on BR");

    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_LEFT), "BR -> left moves");
    ASSERT(&grid.widgets[grid.focus_idx] == bl, "BR -> left lands on BL");

    ASSERT(!ui_grid_move_focus(&grid, UI_EVENT_NAV_LEFT),
           "BL -> left is an edge, no widget that way");
}

static void test_grid_hit_test_and_tap(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);

    UiWidget *a = ui_grid_add_button(&grid, (UiRect){0,  0, 50, 50}, "A", probe_activate);
    UiWidget *b = ui_grid_add_button(&grid, (UiRect){60, 0, 50, 50}, "B", probe_activate);
    ASSERT(a && b, "Both buttons allocated");

    ui_grid_focus_first(&grid); /* focused on A */
    ASSERT(&grid.widgets[grid.focus_idx] == a, "focus_first lands on A");

    ASSERT(ui_grid_hit_test(&grid, 25, 25) == 0, "Hit inside A returns index 0");
    ASSERT(ui_grid_hit_test(&grid, 85, 25) == 1, "Hit inside B returns index 1");
    ASSERT(ui_grid_hit_test(&grid, 55, 25) == -1, "Hit in the gap returns -1");

    UiEvent tap = { .type = UI_EVENT_TAP, .x = 85, .y = 25 };
    ASSERT(ui_grid_handle_event(&grid, tap), "Tap on B is handled");
    ASSERT(&grid.widgets[grid.focus_idx] == b, "Tap relocates focus to B");
    ASSERT(probe.activate_count == 1 && probe.last_widget == b,
           "Tap fires the same activation path as a button press — no parallel touch path");

    UiEvent miss = { .type = UI_EVENT_TAP, .x = 55, .y = 25 };
    ASSERT(!ui_grid_handle_event(&grid, miss), "Tap in empty space is not handled");
    ASSERT(probe.activate_count == 1, "Missed tap does not fire activation again");
}

static void test_numeric_and_dropdown_activate_defaults(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);

    UiWidget *height = ui_grid_add_numeric_field(&grid, (UiRect){0, 0, 100, 30},
                                                 "Antenna Ht.", 5.0, 0.5, 4.0, 6.0, "ft", 3);
    static const char *opts[] = { "Bottom of quick release", "Top of pole", "Phase center" };
    UiWidget *measured_to = ui_grid_add_dropdown(&grid, (UiRect){0, 40, 100, 30},
                                                 "Measured to", opts, 3, 0);
    ui_grid_focus_first(&grid);

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };

    ui_grid_handle_event(&grid, activate);
    ASSERT(height->as.numeric.value == 5.5, "Numeric ACTIVATE steps by 0.5 ft");

    for (int i = 0; i < 4; i++) ui_grid_handle_event(&grid, activate);
    ASSERT(height->as.numeric.value == 6.0,
           "Numeric ACTIVATE clamps at max (6.0 ft), does not overshoot or wrap");

    ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN);
    ASSERT(&grid.widgets[grid.focus_idx] == measured_to, "Focus moved down to dropdown");

    ui_grid_handle_event(&grid, activate);
    ASSERT(measured_to->as.dropdown.selected == 1, "Dropdown ACTIVATE cycles forward");

    ui_grid_handle_event(&grid, activate);
    ui_grid_handle_event(&grid, activate);
    ASSERT(measured_to->as.dropdown.selected == 0, "Dropdown ACTIVATE wraps after the last option");
}

/* =========================================================================
 * Screen stack: push / pop / replace / back
 * ========================================================================= */

typedef struct {
    int  enter_count;
    int  exit_count;
    bool consume_back;
} DummyScreenCtx;

static void dummy_enter(void *ctx) { ((DummyScreenCtx *)ctx)->enter_count++; }
static void dummy_exit(void *ctx)  { ((DummyScreenCtx *)ctx)->exit_count++; }

static bool dummy_event(void *ctx, UiEvent ev)
{
    DummyScreenCtx *d = (DummyScreenCtx *)ctx;
    if (ev.type == UI_EVENT_BACK && d->consume_back)
        return true; /* screen handled it itself (e.g. a confirm dialog) */
    return false;
}

static UiScreen make_dummy_screen(DummyScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter = dummy_enter;
    s.on_exit  = dummy_exit;
    s.on_event = dummy_event;
    s.ctx      = ctx;
    return s;
}

static void test_screen_stack_push_pop_back(void)
{
    UiScreenStack  stack;
    DummyScreenCtx root_ctx = {0};
    DummyScreenCtx child_ctx = {0};
    ui_stack_init(&stack);

    ASSERT(ui_stack_push(&stack, make_dummy_screen(&root_ctx)), "Push root screen");
    ASSERT(root_ctx.enter_count == 1, "Root on_enter fired once");

    ASSERT(ui_stack_push(&stack, make_dummy_screen(&child_ctx)), "Push child screen");
    ASSERT(child_ctx.enter_count == 1, "Child on_enter fired once");
    ASSERT(root_ctx.exit_count == 0, "Pushing a screen does NOT exit the one underneath");

    UiEvent back = { .type = UI_EVENT_BACK };
    ui_stack_dispatch_event(&stack, back);
    ASSERT(child_ctx.exit_count == 1, "Unconsumed BACK pops the child (on_exit fires)");
    ASSERT(root_ctx.enter_count == 2, "Root resumes — on_enter fires again as 'resume'");
    ASSERT(ui_stack_top(&stack)->ctx == &root_ctx, "Root is back on top");

    /* At depth 1, BACK must not pop the root off the stack entirely. */
    ui_stack_dispatch_event(&stack, back);
    ASSERT(root_ctx.exit_count == 0, "BACK at depth 1 (root) is a no-op, root is never popped");

    /* Replace: push child again, then replace it with a third screen. */
    ui_stack_push(&stack, make_dummy_screen(&child_ctx));
    DummyScreenCtx third_ctx = {0};
    ui_stack_replace(&stack, make_dummy_screen(&third_ctx));
    ASSERT(child_ctx.exit_count == 2, "Replace exits the screen it's replacing");
    ASSERT(third_ctx.enter_count == 1, "Replace enters the new screen");
    ASSERT(stack.depth == 2, "Replace keeps stack depth the same (root + replacement)");
}

/* =========================================================================
 * Scroll region — D-pad-driven vertical scrolling for forms taller than
 * their viewport (added by the same commit that raised
 * UI_GRID_MAX_WIDGETS to 80; see ui/core/widget.h's UiWidgetGrid::
 * scroll_region doc comment for the full design rationale).
 *
 * Covers what that commit's own message described but never actually
 * committed to this file (caught the same way as tests/test_screens.c's
 * equivalent gap: comparing the commit's diff stat against its own
 * commit message). Written here from the real ui_grid_set_scroll_region()
 * / ui_widget_mark_scrollable() / ui_grid_move_focus() / ui_grid_hit_test()
 * implementations in widget.c, not from the prior description.
 * ========================================================================= */

/**
 * No scroll region set (the ui_grid_init() default, all-zero) — every
 * existing screen (Main Menu, Sleep, New Project) relies on this exact
 * backward-compatible behavior: scrolling must be a complete no-op when
 * scroll_region is zero-area, regardless of widget count or position.
 */
static void test_scroll_disabled_by_default(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);

    ASSERT(grid.scroll_region.w == 0 && grid.scroll_region.h == 0,
          "ui_grid_init leaves scroll_region zero-area by default");

    UiWidget *a = ui_grid_add_button(&grid, (UiRect){0, 0,   50, 50}, "A", probe_activate);
    UiWidget *b = ui_grid_add_button(&grid, (UiRect){0, 600, 50, 50}, "B", probe_activate);
    ui_widget_mark_scrollable(a);
    ui_widget_mark_scrollable(b);

    ui_grid_focus_first(&grid);
    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN), "Focus moves from A to B");
    ASSERT(&grid.widgets[grid.focus_idx] == b, "B is focused despite being far below A");
    ASSERT(grid.scroll_y == 0,
          "scroll_y stays 0 with no scroll region set, even for a scrollable widget far off-screen");

    UiRect eff = ui_widget_effective_rect(b, &grid);
    ASSERT(eff.y == 600, "Effective rect equals the literal rect when scrolling is disabled");

    ASSERT(ui_grid_hit_test(&grid, 25, 625) == 1,
          "Hit-testing uses literal position when scrolling is disabled");
}

/**
 * Auto-scroll-down: focus moves to a scrollable widget below the visible
 * region, and ui_grid_move_focus() scrolls just enough to bring its
 * bottom edge to the region's bottom edge -- not more, not less.
 */
static void test_scroll_auto_scroll_down(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);
    ui_grid_set_scroll_region(&grid, (UiRect){0, 0, 800, 100}); /* viewport: y in [0,100) */

    UiWidget *a = ui_grid_add_button(&grid, (UiRect){0, 0,   50, 30}, "A", probe_activate);
    UiWidget *b = ui_grid_add_button(&grid, (UiRect){0, 150, 50, 30}, "B", probe_activate);
    ui_widget_mark_scrollable(a);
    ui_widget_mark_scrollable(b);

    ui_grid_focus_first(&grid);
    ASSERT(grid.scroll_y == 0, "ui_grid_focus_first resets scroll_y to 0");

    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN), "Focus moves from A to B");
    ASSERT(&grid.widgets[grid.focus_idx] == b, "B is focused");

    /* B's literal rect is y=150..180. Region bottom is 100. Scrolling by
     * exactly 80 puts B's effective bottom (180 - 80 = 100) flush with
     * the region's bottom -- the documented "just enough" behavior, not
     * scrolled past it. */
    ASSERT(grid.scroll_y == 80,
          "Auto-scroll-down moves scroll_y by exactly enough to bring B's bottom into view");

    UiRect eff = ui_widget_effective_rect(b, &grid);
    ASSERT(eff.y == 70, "B's effective top after scrolling is 150 - 80 = 70");
}

/**
 * Auto-scroll-up exercises the documented int32_t wraparound fix
 * directly: starting already scrolled down, moving focus back up to a
 * widget whose effective position would be negative if miscomputed in
 * uint16_t (e.g. -16 wrapping to 65520). ui_widget_effective_rect()
 * clamps negative effective positions to 0 by design (see its own doc
 * comment in widget.c) -- the wraparound bug this guards against is in
 * the *comparison/accumulator* arithmetic inside ui_grid_move_focus(),
 * not in the rect this test reads back, so this test's real assertion is
 * scroll_y itself landing on the correct signed value rather than a
 * uint16_t-wrapped one.
 */
static void test_scroll_auto_scroll_up(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);
    ui_grid_set_scroll_region(&grid, (UiRect){0, 0, 800, 100}); /* viewport: y in [0,100) */

    UiWidget *a = ui_grid_add_button(&grid, (UiRect){0, 0,   50, 30}, "A", probe_activate);
    UiWidget *b = ui_grid_add_button(&grid, (UiRect){0, 150, 50, 30}, "B", probe_activate);
    ui_widget_mark_scrollable(a);
    ui_widget_mark_scrollable(b);

    ui_grid_focus_first(&grid);
    ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN); /* A -> B, scrolls down to scroll_y == 80 */
    ASSERT(grid.scroll_y == 80, "Pre-condition: scrolled down to B as in the down-scroll test");

    ASSERT(ui_grid_move_focus(&grid, UI_EVENT_NAV_UP), "Focus moves back from B to A");
    ASSERT(&grid.widgets[grid.focus_idx] == a, "A is focused again");

    /* A's literal rect is y=0..30. At scroll_y=80, A's effective top
     * would be 0 - 80 = -16 if computed naively -- exactly the
     * documented bug value that wraps a uint16_t to 65520. The fix keeps
     * this comparison in int32_t, so scroll_y must come back down by
     * exactly 80 (the amount needed to bring A's top, currently at
     * effective -80, up to the region's top at 0) and land at 0, not at
     * some uint16_t-corrupted value. */
    ASSERT(grid.scroll_y == 0,
          "Auto-scroll-up brings scroll_y back to exactly 0, not a uint16_t-wrapped value");

    UiRect eff = ui_widget_effective_rect(a, &grid);
    ASSERT(eff.y == 0, "A's effective top after scrolling back up is correctly 0, not 65520");
}

/**
 * Hit-testing must use each widget's effective (scrolled) position, both
 * before and after a scroll -- a tap always hits whatever is visually
 * under it, not whatever sat at that literal pixel offset before
 * scrolling.
 */
static void test_scroll_hit_test_before_and_after(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);
    ui_grid_set_scroll_region(&grid, (UiRect){0, 0, 800, 100});

    UiWidget *a = ui_grid_add_button(&grid, (UiRect){0, 0,   50, 30}, "A", probe_activate);
    UiWidget *b = ui_grid_add_button(&grid, (UiRect){0, 150, 50, 30}, "B", probe_activate);
    ui_widget_mark_scrollable(a);
    ui_widget_mark_scrollable(b);
    ui_grid_focus_first(&grid);

    /* Before scrolling: B's literal rect (y=150) is off-screen below a
     * 100px viewport, so nothing at y=10 hits it -- only A. */
    ASSERT(ui_grid_hit_test(&grid, 10, 10) == 0, "Before scrolling, a tap at y=10 hits A");
    ASSERT(ui_grid_hit_test(&grid, 10, 70) == -1,
          "Before scrolling, a tap where B will later appear hits nothing yet");

    ui_grid_move_focus(&grid, UI_EVENT_NAV_DOWN); /* scrolls scroll_y to 80, B effective y=70 */
    ASSERT(grid.scroll_y == 80, "Scrolled down as expected");

    /* After scrolling: A's effective top is 0 - 80 = -16, which
     * ui_grid_hit_test() explicitly excludes (see its own "scrolled
     * above the screen -- can't be tapped" comment) rather than letting
     * it wrap. B's effective top is now 150 - 80 = 70, squarely in the
     * region, and a tap there must hit B. */
    ASSERT(ui_grid_hit_test(&grid, 10, 10) == -1,
          "After scrolling, A is scrolled above the screen and can no longer be tapped");
    ASSERT(ui_grid_hit_test(&grid, 10, 80) == 1,
          "After scrolling, a tap at B's new effective position hits B");

    UiEvent tap = { .type = UI_EVENT_TAP, .x = 10, .y = 80 };
    ASSERT(ui_grid_handle_event(&grid, tap), "The scroll-aware tap is handled");
    ASSERT(probe.last_widget == b, "The scroll-aware tap activates B, not A");
}

/**
 * Capacity regression: Job Create's real-world widget count (20 form
 * widgets + 41 keyboard keys = 61) must fit under UI_GRID_MAX_WIDGETS.
 * This is deliberately a raw count, not a call into job_create_screen.c
 * itself (that full integration is covered by
 * tests/test_screens.c's end-to-end Job Create test) -- this test exists
 * so a future cap reduction is caught here, against the bare number,
 * before it surfaces as a confusing "keyboard drops keys" symptom in a
 * screen test that has many other moving parts.
 */
static void test_scroll_grid_capacity_61_widgets(void)
{
    ActivateProbe probe = {0};
    UiWidgetGrid   grid;
    ui_grid_init(&grid, &probe);

    ASSERT(UI_GRID_MAX_WIDGETS >= 61,
          "UI_GRID_MAX_WIDGETS covers Job Create's real-world 61-widget count");

    int allocated = 0;
    for (int i = 0; i < 61; i++) {
        UiWidget *w = ui_grid_add_button(&grid, (UiRect){0, (uint16_t)(i * 10), 50, 8},
                                         "W", probe_activate);
        if (w) allocated++;
    }
    ASSERT(allocated == 61, "All 61 widgets were allocated without the grid reporting full");
    ASSERT(grid.count == 61, "grid.count reflects all 61 widgets");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_grid_focus_navigation();
    test_grid_hit_test_and_tap();
    test_numeric_and_dropdown_activate_defaults();
    test_screen_stack_push_pop_back();
    test_scroll_disabled_by_default();
    test_scroll_auto_scroll_down();
    test_scroll_auto_scroll_up();
    test_scroll_hit_test_before_and_after();
    test_scroll_grid_capacity_61_widgets();

    if (g_tests_failed == 0) {
        printf("All %d widget/screen-stack tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d widget/screen-stack tests FAILED.\n",
                g_tests_failed, g_tests_run);
        return 1;
    }
}