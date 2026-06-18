#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/ui/core/screen_stack.h"
#include "../src/ui/screens/main_menu_screen.h"
#include "../src/ui/screens/placeholder_screen.h"
#include "../src/ui/screens/sleep_screen.h"

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
 * Stub destination screens (stand-ins for New Project / Job Setup /
 * Measure Points, none of which exist yet).
 * ========================================================================= */

typedef struct {
    int enter_count;
} StubScreenCtx;

static void stub_enter(void *ctx) { ((StubScreenCtx *)ctx)->enter_count++; }
static bool stub_event(void *ctx, UiEvent ev) { (void)ctx; (void)ev; return false; }

static UiScreen make_stub(StubScreenCtx *ctx)
{
    UiScreen s = {0};
    s.on_enter = stub_enter;
    s.on_event = stub_event;
    s.ctx      = ctx;
    return s;
}

/* =========================================================================
 * End-to-end: Sleep -> Main Menu -> stub -> back -> back -> Sleep
 * ========================================================================= */

static void test_sleep_to_menu_to_stub_and_back(void)
{
    UiScreenStack stack;
    ui_stack_init(&stack);

    StubScreenCtx new_project_ctx  = {0};
    StubScreenCtx continue_ctx     = {0};
    StubScreenCtx stats_ctx        = {0};

    MainMenuScreenCtx menu_ctx;
    main_menu_screen_init(&menu_ctx, &stack,
                          make_stub(&new_project_ctx),
                          make_stub(&continue_ctx),
                          make_stub(&stats_ctx));

    SleepScreenCtx sleep_ctx;
    sleep_screen_init(&sleep_ctx, &stack, main_menu_screen_as_ui_screen(&menu_ctx));

    ui_stack_push(&stack, sleep_screen_as_ui_screen(&sleep_ctx));
    ASSERT(stack.depth == 1, "Sleep is the root screen");
    ASSERT(ui_stack_top(&stack)->ctx == &sleep_ctx, "Sleep is on top");

    /* A d-pad press wakes Sleep into the Main Menu. */
    UiEvent nav_down = { .type = UI_EVENT_NAV_DOWN };
    ui_stack_dispatch_event(&stack, nav_down);
    ASSERT(stack.depth == 2, "Waking pushes Main Menu");
    ASSERT(ui_stack_top(&stack)->ctx == &menu_ctx, "Main Menu is on top after waking");
    ASSERT(menu_ctx.grid.focus_idx == 0, "Main Menu focuses the first button on entry");

    /* Activate "Start New Project" (focus_idx 0). */
    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    ui_stack_dispatch_event(&stack, activate);
    ASSERT(stack.depth == 3, "Activating pushes the New Project stub");
    ASSERT(ui_stack_top(&stack)->ctx == &new_project_ctx, "New Project stub is on top");
    ASSERT(new_project_ctx.enter_count == 1, "New Project stub's on_enter fired");

    /* Stub doesn't consume BACK -> default policy pops it. */
    UiEvent back = { .type = UI_EVENT_BACK };
    ui_stack_dispatch_event(&stack, back);
    ASSERT(stack.depth == 2, "Unconsumed BACK pops the stub");
    ASSERT(ui_stack_top(&stack)->ctx == &menu_ctx, "Back on the stub returns to Main Menu");

    /* Navigate to the third button and activate it. */
    ui_stack_dispatch_event(&stack, nav_down);
    ui_stack_dispatch_event(&stack, nav_down);
    ASSERT(menu_ctx.grid.focus_idx == 2, "Two NAV_DOWNs reach the third button (Stats)");
    ui_stack_dispatch_event(&stack, activate);
    ASSERT(ui_stack_top(&stack)->ctx == &stats_ctx, "Activating the third button pushes Stats stub");

    /* Back out of Stats, then back out of Main Menu -> Sleep, and Sleep
     * itself is never popped off the stack. */
    ui_stack_dispatch_event(&stack, back);
    ASSERT(ui_stack_top(&stack)->ctx == &menu_ctx, "Back from Stats returns to Main Menu");

    ui_stack_dispatch_event(&stack, back);
    ASSERT(stack.depth == 1, "Back from Main Menu returns to Sleep");
    ASSERT(ui_stack_top(&stack)->ctx == &sleep_ctx, "Sleep is on top again");

    ui_stack_dispatch_event(&stack, back);
    ASSERT(stack.depth == 1, "Back at the root (Sleep) is a no-op -- never pops to empty");
}

/* =========================================================================
 * Placeholder screen: never consumes anything, including BACK
 * ========================================================================= */

static void test_placeholder_always_unconsumed(void)
{
    PlaceholderScreenCtx ctx;
    placeholder_screen_init(&ctx, "Test message");

    UiScreen s = placeholder_screen_as_ui_screen(&ctx);

    UiEvent activate = { .type = UI_EVENT_ACTIVATE };
    ASSERT(!s.on_event(s.ctx, activate), "Placeholder never consumes ACTIVATE");

    UiEvent back = { .type = UI_EVENT_BACK };
    ASSERT(!s.on_event(s.ctx, back), "Placeholder lets BACK fall through to the stack");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_sleep_to_menu_to_stub_and_back();
    test_placeholder_always_unconsumed();

    if (g_tests_failed == 0) {
        printf("All %d screen tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d screen tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}