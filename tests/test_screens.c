#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/collector/job_metadata.h"
#include "../src/ui/core/screen_stack.h"
#include "../src/ui/screens/continue_project_screen.h"
#include "../src/ui/screens/job_create_screen.h"
#include "../src/ui/screens/job_setup_screen.h"
#include "../src/ui/screens/main_menu_screen.h"
#include "../src/ui/screens/new_project_screen.h"
#include "../src/ui/screens/open_job_screen.h"
#include "../src/ui/screens/placeholder_screen.h"
#include "../src/ui/screens/project_context.h"
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

    ProjectContext project_ctx;
    project_context_init(&project_ctx);

    PlaceholderScreenCtx job_setup_stub;
    PlaceholderScreenCtx continue_stub;
    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&job_setup_stub, "Job Setup -- not built yet");
    placeholder_screen_init(&continue_stub,  "Continue Project -- not built yet");
    placeholder_screen_init(&stats_stub,     "Stats -- not built yet");

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            placeholder_screen_as_ui_screen(&job_setup_stub),
                            &project_ctx);

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
    ASSERT(strcmp(project_ctx.name, name) == 0,
          "Create writes the project name into the shared ProjectContext");

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

    ProjectContext project_ctx;
    project_context_init(&project_ctx);

    PlaceholderScreenCtx measure_points_stub;
    PlaceholderScreenCtx continue_stub;
    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");
    placeholder_screen_init(&continue_stub,       "Continue Project -- not built yet");
    placeholder_screen_init(&stats_stub,          "Stats -- not built yet");

    JobCreateScreenCtx job_create_ctx;
    job_create_screen_init(&job_create_ctx, &stack,
                           placeholder_screen_as_ui_screen(&measure_points_stub),
                           &project_ctx);

    OpenJobScreenCtx open_job_ctx;
    open_job_screen_init(&open_job_ctx, &stack,
                         placeholder_screen_as_ui_screen(&measure_points_stub),
                         &project_ctx);

    JobSetupScreenCtx job_setup_ctx;
    job_setup_screen_init(&job_setup_ctx, &stack,
                          job_create_screen_as_ui_screen(&job_create_ctx),
                          open_job_screen_as_ui_screen(&open_job_ctx));

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            job_setup_screen_as_ui_screen(&job_setup_ctx),
                            &project_ctx);

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

    /* Confirm job.ini actually exists on disk, under the real project
     * name created above -- this is the seam fix: job_create_screen.c
     * used to hardcode a "default-project" placeholder here (see this
     * test's own prior history / job_create_screen.h's doc comment for
     * what that gap was); ProjectContext now threads the actual project
     * name through, so jobs land where they should. */
    char job_dir[600];
    snprintf(job_dir, sizeof(job_dir), "%s/geomark-data/projects/%s/%s",
             tmp_home, project_name, job_name);
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

    /* Clean up the disposable HOME tree this test created. Now that the
     * job is threaded under the real project name (project_ctx, not a
     * separate "default-project" placeholder), there is only one tree to
     * remove: tmp_home/geomark-data/projects/TESTPROJ/TESTJOB. This is
     * simpler than this test's prior shape, which had to clean up two
     * independent subtrees under the shared geomark-data/projects parent. */
    unlink(ini_path);
    rmdir(job_dir);

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
 * End-to-end: Open Existing Job. Builds on the same flow as
 * test_job_setup_and_create_end_to_end (Sleep -> Main Menu -> New Project
 * -> Job Setup -> Create New Job) to get one real job.ini onto disk under
 * the real project name, then backs out to Job Setup and drives "Open
 * Existing Job" instead -- confirming the list is populated by an actual
 * directory scan (not a fixture), selecting the job loads its job.ini
 * back into OpenJobScreenCtx, and the screen pushes Measure Points on
 * success, mirroring Job Create's own success path.
 * ========================================================================= */

static void test_open_job_end_to_end(void)
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

    ProjectContext project_ctx;
    project_context_init(&project_ctx);

    PlaceholderScreenCtx measure_points_stub;
    PlaceholderScreenCtx continue_stub;
    PlaceholderScreenCtx stats_stub;
    placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");
    placeholder_screen_init(&continue_stub,       "Continue Project -- not built yet");
    placeholder_screen_init(&stats_stub,          "Stats -- not built yet");

    JobCreateScreenCtx job_create_ctx;
    job_create_screen_init(&job_create_ctx, &stack,
                           placeholder_screen_as_ui_screen(&measure_points_stub),
                           &project_ctx);

    OpenJobScreenCtx open_job_ctx;
    open_job_screen_init(&open_job_ctx, &stack,
                         placeholder_screen_as_ui_screen(&measure_points_stub),
                         &project_ctx);

    JobSetupScreenCtx job_setup_ctx;
    job_setup_screen_init(&job_setup_ctx, &stack,
                          job_create_screen_as_ui_screen(&job_create_ctx),
                          open_job_screen_as_ui_screen(&open_job_ctx));

    NewProjectScreenCtx new_project_ctx;
    new_project_screen_init(&new_project_ctx, &stack,
                            job_setup_screen_as_ui_screen(&job_setup_ctx),
                            &project_ctx);

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
    UiEvent back      = { .type = UI_EVENT_BACK };
    ui_stack_dispatch_event(&stack, nav_down); /* Sleep -> Main Menu      */
    ui_stack_dispatch_event(&stack, activate); /* Start New Project       */

    /* Create a project, same per-letter pattern as the other end-to-end
     * tests above. */
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
          "Create Project pushes Job Setup");

    /* Create a job under that project. */
    UiWidget *create_new_job_btn =
        find_widget(&job_setup_ctx.grid, WIDGET_BUTTON, "Create New Job");
    ASSERT(create_new_job_btn != NULL, "Create New Job button exists on Job Setup");
    if (create_new_job_btn)
        activate_widget_directly(&job_setup_ctx.grid, create_new_job_btn);

    ASSERT(ui_stack_top(&stack)->ctx == &job_create_ctx,
          "Create New Job pushes Job Create");

    const char *job_name = "TESTJOB";
    UiWidget *job_name_field =
        find_widget(&job_create_ctx.grid, WIDGET_TEXT_FIELD, "Job Name");
    ASSERT(job_name_field != NULL, "Job Name text field exists on Job Create");
    if (job_name_field)
        activate_widget_directly(&job_create_ctx.grid, job_name_field);

    for (const char *p = job_name; *p; p++) {
        UiWidget *key = find_widget(&job_create_ctx.grid, WIDGET_BUTTON,
                                    (char[]){ *p, '\0' });
        ASSERT(key != NULL, "Each letter of the test job name has a matching key");
        if (key) activate_widget_directly(&job_create_ctx.grid, key);
    }

    UiWidget *create_job_btn =
        find_widget(&job_create_ctx.grid, WIDGET_BUTTON, "Create Job");
    ASSERT(create_job_btn != NULL, "Create Job button exists");
    if (create_job_btn)
        activate_widget_directly(&job_create_ctx.grid, create_job_btn);

    ASSERT(ui_stack_top(&stack)->ctx == &measure_points_stub,
          "Create Job pushes Measure Points -- job.ini is now on disk");

    /* Back out of Job Create and Measure Points isn't pushed again --
     * BACK is unconsumed by the placeholder, popping to Job Create, then
     * BACK again pops Job Create back to Job Setup (both screens'
     * on_event return false for UI_EVENT_BACK, see job_create_screen.c /
     * job_setup_screen.c, so the stack's default pop policy handles both
     * hops). */
    ui_stack_dispatch_event(&stack, back); /* Measure Points stub -> Job Create */
    ui_stack_dispatch_event(&stack, back); /* Job Create -> Job Setup           */
    ASSERT(ui_stack_top(&stack)->ctx == &job_setup_ctx,
          "Two BACKs return to Job Setup after creating a job");

    /* Now drive "Open Existing Job" instead. */
    UiWidget *open_job_btn =
        find_widget(&job_setup_ctx.grid, WIDGET_BUTTON, "Open Existing Job");
    ASSERT(open_job_btn != NULL, "Open Existing Job button exists on Job Setup");
    if (open_job_btn)
        activate_widget_directly(&job_setup_ctx.grid, open_job_btn);

    ASSERT(ui_stack_top(&stack)->ctx == &open_job_ctx,
          "Open Existing Job pushes the real Open Job screen, not a placeholder");

    /* on_enter (fired by ui_stack_push above) must have scanned the
     * project directory and found exactly the one job just created --
     * this is the actual proof the directory-scan code works, not a
     * fixture standing in for it. */
    ASSERT(open_job_ctx.status == OPEN_JOB_STATUS_NONE,
          "A project with exactly one job reports no error status");
    ASSERT(open_job_ctx.job_count == 1, "The scan found exactly the one job that was created");
    ASSERT(strcmp(open_job_ctx.job_names[0], job_name) == 0,
          "The discovered job's name matches what was typed during Create");

    UiWidget *job_btn = find_widget(&open_job_ctx.grid, WIDGET_BUTTON, job_name);
    ASSERT(job_btn != NULL, "A button for the discovered job exists in the grid");
    if (job_btn)
        activate_widget_directly(&open_job_ctx.grid, job_btn);

    ASSERT(open_job_ctx.status == OPEN_JOB_STATUS_NONE,
          "Selecting the job reports no error status");
    ASSERT(ui_stack_top(&stack)->ctx == &measure_points_stub,
          "Selecting a job pushes Measure Points, same destination Job Create uses");
    ASSERT(strcmp(open_job_ctx.loaded_meta.job_name, job_name) == 0,
          "The loaded job metadata's name matches the job that was selected");
    ASSERT(open_job_ctx.loaded_meta.coord_sys == GM_COORD_SYS_WGS84,
          "The loaded job metadata's coord_sys matches what Job Create wrote "
          "(WGS84 default -- this test never cycled the dropdown)");

    /* Clean up -- same single-tree shape as
     * test_job_setup_and_create_end_to_end now that there's no separate
     * default-project subtree. */
    char job_dir[600];
    snprintf(job_dir, sizeof(job_dir), "%s/geomark-data/projects/%s/%s",
             tmp_home, project_name, job_name);
    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", job_dir);
    unlink(ini_path);
    rmdir(job_dir);

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

    if (real_home_buf[0])
        setenv("HOME", real_home_buf, 1);
    else
        unsetenv("HOME");
}

/* =========================================================================
 * Open Existing Job: no-project and no-jobs status cases, without going
 * through the full New Project / Job Create flow -- exercises
 * scan_jobs()'s two early-out paths directly via a fresh
 * OpenJobScreenCtx and its own disposable HOME.
 * ========================================================================= */

static void test_open_job_status_cases(void)
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

    PlaceholderScreenCtx measure_points_stub;
    placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");

    /* Case 1: no project set at all. */
    ProjectContext empty_ctx;
    project_context_init(&empty_ctx);

    OpenJobScreenCtx no_project_ctx;
    open_job_screen_init(&no_project_ctx, &stack,
                         placeholder_screen_as_ui_screen(&measure_points_stub),
                         &empty_ctx);
    ui_stack_push(&stack, open_job_screen_as_ui_screen(&no_project_ctx));

    ASSERT(no_project_ctx.status == OPEN_JOB_STATUS_NO_PROJECT,
          "With no project set, Open Job reports OPEN_JOB_STATUS_NO_PROJECT");
    ASSERT(no_project_ctx.job_count == 0,
          "With no project set, the job list is empty");
    ui_stack_pop(&stack);

    /* Case 2: a project exists but has zero job subdirectories. */
    ProjectContext empty_project_ctx;
    project_context_init(&empty_project_ctx);
    project_context_set(&empty_project_ctx, "EMPTYPROJ");

    char base[300], projects[330], project[400];
    snprintf(base,     sizeof(base),     "%s/geomark-data", tmp_home);
    snprintf(projects, sizeof(projects), "%s/projects", base);
    snprintf(project,  sizeof(project),  "%s/EMPTYPROJ", projects);
    mkdir(base, 0755);
    mkdir(projects, 0755);
    mkdir(project, 0755);

    OpenJobScreenCtx no_jobs_ctx;
    open_job_screen_init(&no_jobs_ctx, &stack,
                         placeholder_screen_as_ui_screen(&measure_points_stub),
                         &empty_project_ctx);
    ui_stack_push(&stack, open_job_screen_as_ui_screen(&no_jobs_ctx));

    ASSERT(no_jobs_ctx.status == OPEN_JOB_STATUS_NO_JOBS,
          "An existing project with zero jobs reports OPEN_JOB_STATUS_NO_JOBS");
    ASSERT(no_jobs_ctx.job_count == 0,
          "An existing project with zero jobs has an empty job list");
    ui_stack_pop(&stack);

    rmdir(project);
    rmdir(projects);
    rmdir(base);
    rmdir(tmp_home);

    if (real_home_buf[0])
        setenv("HOME", real_home_buf, 1);
    else
        unsetenv("HOME");
}

/* =========================================================================
 * End-to-end: Continue Existing Project closes the real gap found during
 * hardware testing -- ProjectContext lives only in memory for one
 * geomark process, so restarting the UI loses track of which project
 * was active even though its directory (and any jobs already created
 * under it) are still on disk.
 *
 * Simulates that restart directly: builds a project + job through the
 * normal flow with one ProjectContext instance, then constructs a
 * SECOND, independent screen stack with a fresh (empty) ProjectContext
 * -- exactly what a real process restart produces -- and confirms
 * Continue Existing Project finds the project on disk, selecting it
 * re-populates ProjectContext, and Job Setup -> Open Existing Job then
 * correctly lists the job that was created before the "restart".
 * ========================================================================= */

static void test_continue_project_end_to_end(void)
{
    char tmpl[] = "/tmp/geomark_test_home_XXXXXX";
    char *tmp_home = mkdtemp(tmpl);
    ASSERT(tmp_home != NULL, "mkdtemp created a disposable HOME for this test");

    const char *real_home = getenv("HOME");
    char real_home_buf[512] = {0};
    if (real_home) strncpy(real_home_buf, real_home, sizeof(real_home_buf) - 1);
    setenv("HOME", tmp_home, 1);

    const char *project_name = "TESTPROJ";
    const char *job_name     = "TESTJOB";

    /* --- "First process run": create a project and a job in it. --- */
    {
        UiScreenStack stack;
        ui_stack_init(&stack);

        ProjectContext project_ctx;
        project_context_init(&project_ctx);

        PlaceholderScreenCtx measure_points_stub;
        PlaceholderScreenCtx stats_stub;
        placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");
        placeholder_screen_init(&stats_stub,          "Stats -- not built yet");

        JobCreateScreenCtx job_create_ctx;
        job_create_screen_init(&job_create_ctx, &stack,
                               placeholder_screen_as_ui_screen(&measure_points_stub),
                               &project_ctx);

        OpenJobScreenCtx open_job_ctx;
        open_job_screen_init(&open_job_ctx, &stack,
                             placeholder_screen_as_ui_screen(&measure_points_stub),
                             &project_ctx);

        JobSetupScreenCtx job_setup_ctx;
        job_setup_screen_init(&job_setup_ctx, &stack,
                              job_create_screen_as_ui_screen(&job_create_ctx),
                              open_job_screen_as_ui_screen(&open_job_ctx));

        ContinueProjectScreenCtx continue_project_ctx;
        continue_project_screen_init(&continue_project_ctx, &stack,
                                     job_setup_screen_as_ui_screen(&job_setup_ctx),
                                     &project_ctx);

        NewProjectScreenCtx new_project_ctx;
        new_project_screen_init(&new_project_ctx, &stack,
                                job_setup_screen_as_ui_screen(&job_setup_ctx),
                                &project_ctx);

        MainMenuScreenCtx menu_ctx;
        main_menu_screen_init(&menu_ctx, &stack,
                              new_project_screen_as_ui_screen(&new_project_ctx),
                              continue_project_screen_as_ui_screen(&continue_project_ctx),
                              placeholder_screen_as_ui_screen(&stats_stub));

        SleepScreenCtx sleep_ctx;
        sleep_screen_init(&sleep_ctx, &stack, main_menu_screen_as_ui_screen(&menu_ctx));
        ui_stack_push(&stack, sleep_screen_as_ui_screen(&sleep_ctx));

        UiEvent nav_down = { .type = UI_EVENT_NAV_DOWN };
        UiEvent activate  = { .type = UI_EVENT_ACTIVATE };
        ui_stack_dispatch_event(&stack, nav_down);
        ui_stack_dispatch_event(&stack, activate);

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

        UiWidget *create_new_job_btn =
            find_widget(&job_setup_ctx.grid, WIDGET_BUTTON, "Create New Job");
        ASSERT(create_new_job_btn != NULL, "Create New Job button exists on Job Setup");
        if (create_new_job_btn)
            activate_widget_directly(&job_setup_ctx.grid, create_new_job_btn);

        UiWidget *job_name_field =
            find_widget(&job_create_ctx.grid, WIDGET_TEXT_FIELD, "Job Name");
        ASSERT(job_name_field != NULL, "Job Name text field exists on Job Create");
        if (job_name_field)
            activate_widget_directly(&job_create_ctx.grid, job_name_field);

        for (const char *p = job_name; *p; p++) {
            UiWidget *key = find_widget(&job_create_ctx.grid, WIDGET_BUTTON,
                                        (char[]){ *p, '\0' });
            ASSERT(key != NULL, "Each letter of the test job name has a matching key");
            if (key) activate_widget_directly(&job_create_ctx.grid, key);
        }

        UiWidget *create_job_btn =
            find_widget(&job_create_ctx.grid, WIDGET_BUTTON, "Create Job");
        ASSERT(create_job_btn != NULL, "Create Job button exists");
        if (create_job_btn)
            activate_widget_directly(&job_create_ctx.grid, create_job_btn);

        ASSERT(ui_stack_top(&stack)->ctx == &measure_points_stub,
              "Create Job pushes Measure Points -- job.ini is now on disk");
    }
    /* Every ctx above goes out of scope here -- this is the "process
     * restart": nothing carries over except what's actually on disk,
     * exactly matching what a real Ctrl+C / relaunch of geomark loses
     * (in-memory ProjectContext) versus keeps (the project/job
     * directories and job.ini files this block wrote). */

    /* --- "Second process run": fresh stack, fresh empty ProjectContext,
     * same disposable HOME. Continue Existing Project must find the
     * project that the first block created. --- */
    {
        UiScreenStack stack;
        ui_stack_init(&stack);

        ProjectContext project_ctx;
        project_context_init(&project_ctx);
        ASSERT(!project_context_has_project(&project_ctx),
              "The 'restarted' ProjectContext starts with no project set, "
              "matching what a real process restart loses");

        PlaceholderScreenCtx measure_points_stub;
        PlaceholderScreenCtx stats_stub;
        placeholder_screen_init(&measure_points_stub, "Measure Points -- not built yet");
        placeholder_screen_init(&stats_stub,          "Stats -- not built yet");

        JobCreateScreenCtx job_create_ctx;
        job_create_screen_init(&job_create_ctx, &stack,
                               placeholder_screen_as_ui_screen(&measure_points_stub),
                               &project_ctx);

        OpenJobScreenCtx open_job_ctx;
        open_job_screen_init(&open_job_ctx, &stack,
                             placeholder_screen_as_ui_screen(&measure_points_stub),
                             &project_ctx);

        JobSetupScreenCtx job_setup_ctx;
        job_setup_screen_init(&job_setup_ctx, &stack,
                              job_create_screen_as_ui_screen(&job_create_ctx),
                              open_job_screen_as_ui_screen(&open_job_ctx));

        ContinueProjectScreenCtx continue_project_ctx;
        continue_project_screen_init(&continue_project_ctx, &stack,
                                     job_setup_screen_as_ui_screen(&job_setup_ctx),
                                     &project_ctx);

        NewProjectScreenCtx new_project_ctx;
        new_project_screen_init(&new_project_ctx, &stack,
                                job_setup_screen_as_ui_screen(&job_setup_ctx),
                                &project_ctx);

        MainMenuScreenCtx menu_ctx;
        main_menu_screen_init(&menu_ctx, &stack,
                              new_project_screen_as_ui_screen(&new_project_ctx),
                              continue_project_screen_as_ui_screen(&continue_project_ctx),
                              placeholder_screen_as_ui_screen(&stats_stub));

        SleepScreenCtx sleep_ctx;
        sleep_screen_init(&sleep_ctx, &stack, main_menu_screen_as_ui_screen(&menu_ctx));
        ui_stack_push(&stack, sleep_screen_as_ui_screen(&sleep_ctx));

        UiEvent nav_down = { .type = UI_EVENT_NAV_DOWN };
        UiEvent nav_down2 = { .type = UI_EVENT_NAV_DOWN };
        UiEvent activate  = { .type = UI_EVENT_ACTIVATE };
        ui_stack_dispatch_event(&stack, nav_down);  /* Sleep -> Main Menu */

        /* "Continue Existing Project" is the second button (index 1) --
         * one NAV_DOWN from the first button Main Menu focuses by
         * default, same convention test_sleep_to_menu_to_stub_and_back
         * uses to reach the third button with two NAV_DOWNs. */
        ui_stack_dispatch_event(&stack, nav_down2);
        ASSERT(menu_ctx.grid.focus_idx == 1,
              "One NAV_DOWN from Main Menu's default focus reaches the second button");
        ui_stack_dispatch_event(&stack, activate);

        ASSERT(ui_stack_top(&stack)->ctx == &continue_project_ctx,
              "Continue Existing Project pushes the real screen, not a placeholder");
        ASSERT(continue_project_ctx.status == CONTINUE_PROJECT_STATUS_NONE,
              "A geomark-data tree with one project reports no error status");
        ASSERT(continue_project_ctx.project_count == 1,
              "The scan found exactly the one project created in the prior block");
        ASSERT(strcmp(continue_project_ctx.project_names[0], project_name) == 0,
              "The discovered project's name matches what New Project created earlier");

        UiWidget *project_btn =
            find_widget(&continue_project_ctx.grid, WIDGET_BUTTON, project_name);
        ASSERT(project_btn != NULL, "A button for the discovered project exists in the grid");
        if (project_btn)
            activate_widget_directly(&continue_project_ctx.grid, project_btn);

        ASSERT(ui_stack_top(&stack)->ctx == &job_setup_ctx,
              "Selecting a project pushes Job Setup, same destination New Project's "
              "Create button uses");
        ASSERT(strcmp(project_ctx.name, project_name) == 0,
              "Selecting the project wrote its name into the shared ProjectContext");

        /* Now that ProjectContext is repopulated, Open Existing Job must
         * find the job created in the "first process run" -- this is
         * the actual end-to-end proof the gap is closed, not just that
         * ProjectContext got a string written into it. */
        UiWidget *open_job_btn =
            find_widget(&job_setup_ctx.grid, WIDGET_BUTTON, "Open Existing Job");
        ASSERT(open_job_btn != NULL, "Open Existing Job button exists on Job Setup");
        if (open_job_btn)
            activate_widget_directly(&job_setup_ctx.grid, open_job_btn);

        ASSERT(ui_stack_top(&stack)->ctx == &open_job_ctx,
              "Open Existing Job pushes the real screen");
        ASSERT(open_job_ctx.status == OPEN_JOB_STATUS_NONE,
              "Open Existing Job reports no error status for the recovered project");
        ASSERT(open_job_ctx.job_count == 1,
              "Open Existing Job finds the one job created before the simulated restart");
        ASSERT(strcmp(open_job_ctx.job_names[0], job_name) == 0,
              "The recovered job's name matches what was created in the prior block");
    }

    /* Clean up the disposable HOME tree both blocks shared. */
    char job_dir[600];
    snprintf(job_dir, sizeof(job_dir), "%s/geomark-data/projects/%s/%s",
             tmp_home, project_name, job_name);
    char ini_path[640];
    snprintf(ini_path, sizeof(ini_path), "%s/job.ini", job_dir);
    unlink(ini_path);
    rmdir(job_dir);

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

    if (real_home_buf[0])
        setenv("HOME", real_home_buf, 1);
    else
        unsetenv("HOME");
}

/* =========================================================================
 * Continue Existing Project: no-projects status case, without going
 * through New Project at all -- exercises scan_projects()'s early-out
 * path directly via a fresh ContinueProjectScreenCtx and its own
 * disposable HOME (geomark-data/projects never created).
 * ========================================================================= */

static void test_continue_project_status_cases(void)
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
    placeholder_screen_init(&job_setup_stub, "Job Setup -- not built yet");

    ProjectContext project_ctx;
    project_context_init(&project_ctx);

    ContinueProjectScreenCtx no_projects_ctx;
    continue_project_screen_init(&no_projects_ctx, &stack,
                                 placeholder_screen_as_ui_screen(&job_setup_stub),
                                 &project_ctx);
    ui_stack_push(&stack, continue_project_screen_as_ui_screen(&no_projects_ctx));

    ASSERT(no_projects_ctx.status == CONTINUE_PROJECT_STATUS_NO_PROJECTS,
          "With no geomark-data/projects directory at all, Continue Project "
          "reports CONTINUE_PROJECT_STATUS_NO_PROJECTS");
    ASSERT(no_projects_ctx.project_count == 0,
          "With no projects directory, the project list is empty");
    ASSERT(!project_context_has_project(&project_ctx),
          "ProjectContext is untouched when there is nothing to select");

    rmdir(tmp_home);

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
    test_open_job_end_to_end();
    test_open_job_status_cases();
    test_continue_project_end_to_end();
    test_continue_project_status_cases();

    if (g_tests_failed == 0) {
        printf("All %d screen tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d screen tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}