#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/ui/core/screen_stack.h"
#include "../src/ui/screens/main_menu_screen.h"
#include "../src/ui/screens/new_project_screen.h"
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
 * New Project: the real screen, reachable from Main Menu, end to end --
 * type a name via the on-screen keyboard, Create, land on Job Setup.
 *
 * HOME is redirected to a disposable mkdtemp() directory for the
 * duration of this test so it never touches the real
 * ~/geomark-data -- restored afterward regardless of pass/fail.
 * ========================================================================= */

static void test_new_project_end_to_end(void)
{
    char tmpl[] = "/tmp/geomark_test_home_XXXXXX";
    char *tmp_home = mkdtemp(tmpl);
    ASSERT(tmp_home != NULL, "mkdtemp created a disposable HOME for this test");

    const char *real_home = getenv("HOME");
    char real_home_buf[512] = {0};
    if (real_home) strncpy(real_home_buf, real_home, sizeof(real_home_buf) - 1);
    setenv("HOME", tmp_home, 1);

    UiScreenStack stack;
    ui_stack_init(&stack);

    PlaceholderScreenCtx job_setup_stub;
    PlaceholderScreenCtx continue_stub;
    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&job_setup_stub, "Job Setup -- not built yet");
    placeholder_screen_init(&continue_stub,  "Continue Project -- not built yet");
    placeholder_screen_init(&stats_stub,     "Stats -- not built yet");

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            placeholder_screen_as_ui_screen(&job_setup_stub));

    MainMenuScreenCtx menu_ctx;
    main_menu_screen_init(&menu_ctx, &stack,
                          new_project_screen_as_ui_screen(&new_project_ctx),
                          placeholder_screen_as_ui_screen(&continue_stub),
                          placeholder_screen_as_ui_screen(&stats_stub));

    SleepScreenCtx sleep_ctx;
    sleep_screen_init(&sleep_ctx, &stack, main_menu_screen_as_ui_screen(&menu_ctx));
    ui_stack_push(&stack, sleep_screen_as_ui_screen(&sleep_ctx));

    UiEvent nav_down = { .type = UI_EVENT_NAV_DOWN };
    UiEvent activate  = { .type = UI_EVENT_ACTIVATE };
    ui_stack_dispatch_event(&stack, nav_down); /* Sleep -> Main Menu */
    ui_stack_dispatch_event(&stack, activate); /* Start New Project    */

    ASSERT(ui_stack_top(&stack)->ctx == &new_project_ctx,
          "The real New Project screen is on top, not a placeholder");
    ASSERT(new_project_ctx.grid.widgets[new_project_ctx.grid.focus_idx].kind
              == WIDGET_TEXT_FIELD,
          "New Project on_enter focuses the name field");

    /* Type "TESTSITE" by activating each letter's key in turn -- proves
     * the keyboard module's keys actually reach this screen's own
     * name_buf through the UiKeyboardTarget-first-member contract, not
     * just in keyboard.c's own isolated tests. */
    const char *name = "TESTSITE";
    for (const char *p = name; *p; p++) {
        int32_t idx = -1;
        for (uint32_t i = 0; i < new_project_ctx.grid.count; i++) {
            UiWidget *w = &new_project_ctx.grid.widgets[i];
            if (w->kind == WIDGET_BUTTON && w->label && w->label[0] == *p
                && w->label[1] == '\0') {
                idx = (int32_t)i;
                break;
            }
        }
        ASSERT(idx >= 0, "Each letter of the test name has a matching key");
        if (idx < 0) continue;
        new_project_ctx.grid.focus_idx = idx;
        ui_grid_handle_event(&new_project_ctx.grid, activate);
    }
    ASSERT(strcmp(new_project_ctx.name_buf, name) == 0,
          "Typing through the keyboard produced the expected name");

    /* Press Create. */
    int32_t create_idx = -1;
    for (uint32_t i = 0; i < new_project_ctx.grid.count; i++) {
        UiWidget *w = &new_project_ctx.grid.widgets[i];
        if (w->kind == WIDGET_BUTTON && w->label &&
            strcmp(w->label, "Create Project") == 0) {
            create_idx = (int32_t)i;
            break;
        }
    }
    ASSERT(create_idx >= 0, "Create Project button exists");
    new_project_ctx.grid.focus_idx = create_idx;
    ui_grid_handle_event(&new_project_ctx.grid, activate);

    ASSERT(new_project_ctx.status == NEW_PROJECT_STATUS_NONE,
          "Create with a valid, new name reports no error status");
    ASSERT(ui_stack_top(&stack)->ctx == &job_setup_stub,
          "Create pushes the Job Setup stub on success");

    char expected_dir[600];
    snprintf(expected_dir, sizeof(expected_dir),
             "%s/geomark-data/projects/%s", tmp_home, name);
    struct stat st;
    ASSERT(stat(expected_dir, &st) == 0 && S_ISDIR(st.st_mode),
          "The project directory was actually created on disk");

    /* Clean up the disposable HOME tree this test created -- a fixed,
     * known three-level structure (tmp_home/geomark-data/projects/<name>),
     * so plain rmdir() in reverse order is enough; no need for a generic
     * recursive-delete helper for what is always exactly this shape. */
    rmdir(expected_dir);
    char projects_dir[560];
    snprintf(projects_dir, sizeof(projects_dir), "%s/geomark-data/projects", tmp_home);
    rmdir(projects_dir);
    char data_dir[540];
    snprintf(data_dir, sizeof(data_dir), "%s/geomark-data", tmp_home);
    rmdir(data_dir);
    rmdir(tmp_home);

    /* Restore the real HOME unconditionally, including on assertion
     * failure above -- this is a test-harness cleanup step, not a
     * pass/fail condition itself. */
    if (real_home_buf[0])
        setenv("HOME", real_home_buf, 1);
    else
        unsetenv("HOME");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_sleep_to_menu_to_stub_and_back();
    test_placeholder_always_unconsumed();
    test_new_project_end_to_end();

    if (g_tests_failed == 0) {
        printf("All %d screen tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d screen tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}