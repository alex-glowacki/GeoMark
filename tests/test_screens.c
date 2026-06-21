#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/collector/job_metadata.h"
#include "../src/ui/core/screen_stack.h"
#include "../src/ui/screens/job_create_screen.h"
#include "../src/ui/screens/job_setup_screen.h"
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
 * Helper: find a widget in a grid by its kind + exact label match.
 * Shared by the Job Create section below -- the existing New Project test
 * above inlines this same loop once for its letter keys and once for its
 * Create button; with two dropdowns and a Create button to drive here,
 * pulling it out avoids a third/fourth/fifth copy of the same loop.
 * ========================================================================= */

static UiWidget *find_widget(UiWidgetGrid *grid, UiWidgetKind kind, const char *label)
{
    for (uint32_t i = 0; i < grid->count; i++) {
        UiWidget *w = &grid->widgets[i];
        if (w->kind == kind && w->label && strcmp(w->label, label) == 0)
            return w;
    }
    return NULL;
}

/** Activate a widget directly via the grid, bypassing focus navigation --
 *  same shortcut the New Project test above already uses for its letter
 *  keys and Create button. */
static void activate_widget_directly(UiWidgetGrid *grid, UiWidget *w)
{
    for (uint32_t i = 0; i < grid->count; i++) {
        if (&grid->widgets[i] == w) {
            grid->focus_idx = (int32_t)i;
            UiEvent activate = { .type = UI_EVENT_ACTIVATE };
            ui_grid_handle_event(grid, activate);
            return;
        }
    }
}

/* =========================================================================
 * End-to-end: Sleep -> Main Menu -> New Project -> Job Setup ->
 * Create New Job, all the way through to a verified job.ini on disk.
 *
 * Covers what Session 21's handoff documented but never actually
 * committed to this file (caught by comparing the commit's diff stat
 * against its own commit message -- tests/test_screens.c was not in
 * that diff despite the message claiming it was extended). Rewritten
 * here from the real job_setup_screen.h / job_create_screen.h /
 * job_metadata.h signatures, not from the prior description.
 *
 * HOME is redirected to a disposable mkdtemp() directory for the
 * duration of this test, same convention as test_new_project_end_to_end
 * above, restored afterward regardless of pass/fail.
 * ========================================================================= */

static void test_job_setup_and_create_end_to_end(void)
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

    PlaceholderScreenCtx open_job_stub;
    PlaceholderScreenCtx measure_points_stub;
    PlaceholderScreenCtx continue_stub;
    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&open_job_stub,       "Open Existing Job -- not built yet");
    placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");
    placeholder_screen_init(&continue_stub,       "Continue Project -- not built yet");
    placeholder_screen_init(&stats_stub,          "Stats -- not built yet");

    JobCreateScreenCtx job_create_ctx;
    job_create_screen_init(&job_create_ctx, &stack,
                           placeholder_screen_as_ui_screen(&measure_points_stub));

    JobSetupScreenCtx job_setup_ctx;
    job_setup_screen_init(&job_setup_ctx, &stack,
                          job_create_screen_as_ui_screen(&job_create_ctx),
                          placeholder_screen_as_ui_screen(&open_job_stub));

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            job_setup_screen_as_ui_screen(&job_setup_ctx));

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
    ui_stack_dispatch_event(&stack, nav_down); /* Sleep -> Main Menu      */
    ui_stack_dispatch_event(&stack, activate); /* Start New Project       */

    /* Type a project name and press Create, exactly like
     * test_new_project_end_to_end above -- this test starts over from
     * Sleep rather than sharing state with that test, since each test
     * function gets its own disposable HOME and screen stack. */
    const char *project_name = "TESTPROJ";
    for (const char *p = project_name; *p; p++) {
        UiWidget *key = NULL;
        for (uint32_t i = 0; i < new_project_ctx.grid.count; i++) {
            UiWidget *w = &new_project_ctx.grid.widgets[i];
            if (w->kind == WIDGET_BUTTON && w->label && w->label[0] == *p
                && w->label[1] == '\0') {
                key = w;
                break;
            }
        }
        ASSERT(key != NULL, "Each letter of the test project name has a matching key");
        if (key) activate_widget_directly(&new_project_ctx.grid, key);
    }
    UiWidget *create_project_btn =
        find_widget(&new_project_ctx.grid, WIDGET_BUTTON, "Create Project");
    ASSERT(create_project_btn != NULL, "Create Project button exists");
    if (create_project_btn)
        activate_widget_directly(&new_project_ctx.grid, create_project_btn);

    ASSERT(ui_stack_top(&stack)->ctx == &job_setup_ctx,
          "Create Project pushes the real Job Setup screen, not a placeholder");

    /* Job Setup: activate "Create New Job". */
    UiWidget *create_new_job_btn =
        find_widget(&job_setup_ctx.grid, WIDGET_BUTTON, "Create New Job");
    ASSERT(create_new_job_btn != NULL, "Create New Job button exists on Job Setup");
    if (create_new_job_btn)
        activate_widget_directly(&job_setup_ctx.grid, create_new_job_btn);

    ASSERT(ui_stack_top(&stack)->ctx == &job_create_ctx,
          "Create New Job pushes the real Job Create screen, not a placeholder");
    ASSERT(job_create_ctx.meta.coord_sys == GM_COORD_SYS_WGS84,
          "Job Create defaults to WGS84 Geographic");
    ASSERT(job_create_ctx.meta.dist_unit == GM_DIST_UNIT_US_SURVEY_FOOT,
          "Job Create defaults to US Survey Foot before any coord-sys change");

    /* Type a job name through the on-screen keyboard, same per-letter
     * pattern as the project name above -- proves the keyboard's
     * UiKeyboardTarget retargeting (set_active_field()) actually reaches
     * meta.job_name through this screen's six-field switch, not just
     * New Project's single always-active field. */
    const char *job_name = "TESTJOB";
    UiWidget *job_name_field =
        find_widget(&job_create_ctx.grid, WIDGET_TEXT_FIELD, "Job Name");
    ASSERT(job_name_field != NULL, "Job Name text field exists on Job Create");
    if (job_name_field)
        activate_widget_directly(&job_create_ctx.grid, job_name_field);
    ASSERT(job_create_ctx.active_field == JOB_CREATE_FIELD_JOB_NAME,
          "Activating the Job Name field retargets the keyboard at it");

    for (const char *p = job_name; *p; p++) {
        UiWidget *key = find_widget(&job_create_ctx.grid, WIDGET_BUTTON,
                                    (char[]){ *p, '\0' });
        ASSERT(key != NULL, "Each letter of the test job name has a matching key");
        if (key) activate_widget_directly(&job_create_ctx.grid, key);
    }
    ASSERT(strcmp(job_create_ctx.meta.job_name, job_name) == 0,
          "Typing through the keyboard produced the expected job name");

    /* Cycle Coord. Sys. forward three times: WGS84 -> UTM -> Local/Site
     * Ground -> ND North (index 3) -- see widget.c's activate_widget(),
     * dropdowns cycle forward by one on every ACTIVATE before on_activate
     * runs, so three activations from the default reaches ND North. */
    ASSERT(job_create_ctx.coord_sys_dropdown != NULL, "coord_sys_dropdown is cached at init");
    for (int i = 0; i < 3; i++)
        activate_widget_directly(&job_create_ctx.grid, job_create_ctx.coord_sys_dropdown);

    ASSERT(job_create_ctx.meta.coord_sys == GM_COORD_SYS_ND_NORTH,
          "Three cycles land on ND North (EPSG:2265)");
    ASSERT(job_create_ctx.meta.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "Selecting ND North auto-locks the metadata's dist_unit to International Foot");
    ASSERT(job_create_ctx.dist_unit_dropdown->as.dropdown.selected
              == (uint32_t)GM_DIST_UNIT_INTL_FOOT,
          "Selecting ND North also snaps the Units dropdown's own displayed value");

    /* Activating the locked Units dropdown directly must snap straight
     * back to International Foot rather than cycling away, even
     * transiently -- see job_create_screen.c's on_dist_unit_activate(). */
    activate_widget_directly(&job_create_ctx.grid, job_create_ctx.dist_unit_dropdown);
    ASSERT(job_create_ctx.meta.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "Activating the locked Units dropdown snaps back to International Foot");
    ASSERT(job_create_ctx.dist_unit_dropdown->as.dropdown.selected
              == (uint32_t)GM_DIST_UNIT_INTL_FOOT,
          "The locked Units dropdown's displayed value also snaps back");

    /* Press Create Job. */
    UiWidget *create_job_btn =
        find_widget(&job_create_ctx.grid, WIDGET_BUTTON, "Create Job");
    ASSERT(create_job_btn != NULL, "Create Job button exists");
    if (create_job_btn)
        activate_widget_directly(&job_create_ctx.grid, create_job_btn);

    ASSERT(job_create_ctx.status == JOB_CREATE_STATUS_NONE,
          "Create with a valid, non-empty job name reports no error status");
    ASSERT(ui_stack_top(&stack)->ctx == &measure_points_stub,
          "Create Job pushes the Measure Points stub on success");

    /* Confirm job.ini actually exists on disk, under the
     * JOB_CREATE_PLACEHOLDER_PROJECT seam documented in
     * job_create_screen.c (jobs are not yet threaded under the actual
     * project name created above -- that's a known, flagged gap, not
     * something this test should silently paper over). */
    char job_dir[600];
    snprintf(job_dir, sizeof(job_dir), "%s/geomark-data/projects/default-project/%s",
             tmp_home, job_name);
    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", job_dir);

    struct stat st;
    ASSERT(stat(job_dir, &st) == 0 && S_ISDIR(st.st_mode),
          "The job directory was actually created on disk");
    ASSERT(stat(ini_path, &st) == 0 && S_ISREG(st.st_mode),
          "job.ini was actually written to disk");

    /* Load it back and confirm the coercion held on what was actually
     * written, not just the in-memory struct -- same round-trip
     * verification convention as tests/test_job_metadata.c. */
    gm_job_metadata_t loaded;
    gm_status_t load_rc = job_metadata_load(ini_path, &loaded);
    ASSERT(load_rc == GM_OK, "job_metadata_load() reads the written file back without error");
    ASSERT(strcmp(loaded.job_name, job_name) == 0,
          "The loaded job name matches what was typed");
    ASSERT(loaded.coord_sys == GM_COORD_SYS_ND_NORTH,
          "The loaded coordinate system is ND North");
    ASSERT(loaded.dist_unit == GM_DIST_UNIT_INTL_FOOT,
          "The loaded distance unit is International Foot -- the coercion held on disk");

    /* Clean up the disposable HOME tree this test created. Unlike
     * test_new_project_end_to_end's fixed three-level shape, this test
     * creates two separate trees under tmp_home -- the project directory
     * New Project made (tmp_home/geomark-data/projects/TESTPROJ) AND the
     * job directory Job Create made under the unrelated placeholder
     * project (tmp_home/geomark-data/projects/default-project/TESTJOB)
     * -- both sharing the same geomark-data/projects parent. An earlier
     * version of this test's cleanup only removed the job subtree and
     * left the New-Project-created project directory (and therefore
     * geomark-data/projects itself) behind, silently failing the final
     * rmdir(tmp_home) below since a non-empty directory can't be
     * removed; both subtrees are now removed before their shared
     * parents. */
    unlink(ini_path);
    rmdir(job_dir);

    char default_project_dir[560];
    snprintf(default_project_dir, sizeof(default_project_dir),
             "%s/geomark-data/projects/default-project", tmp_home);
    rmdir(default_project_dir);

    char testproj_dir[560];
    snprintf(testproj_dir, sizeof(testproj_dir),
             "%s/geomark-data/projects/%s", tmp_home, project_name);
    rmdir(testproj_dir);

    char projects_dir[540];
    snprintf(projects_dir, sizeof(projects_dir), "%s/geomark-data/projects", tmp_home);
    rmdir(projects_dir);

    char data_dir[520];
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
    test_job_setup_and_create_end_to_end();

    if (g_tests_failed == 0) {
        printf("All %d screen tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d screen tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}