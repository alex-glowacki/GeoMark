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
 * main
 * ========================================================================= */
int main(void)
{
    test_grid_focus_navigation();
    test_grid_hit_test_and_tap();
    test_numeric_and_dropdown_activate_defaults();
    test_screen_stack_push_pop_back();

    if (g_tests_failed == 0) {
        printf("All %d widget/screen-stack tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d widget/screen-stack tests FAILED.\n",
                g_tests_failed, g_tests_run);
        return 1;
    }
}