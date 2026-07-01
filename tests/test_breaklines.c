/**
 * @file tests/test_breaklines.c
 * @brief Unit tests for collector/breaklines.h's breaklines_build() --
 *        see that header's file-level doc comment for the full
 *        algorithm these tests verify against Alex's own worked
 *        examples (confirmed interactively before this module was
 *        written, see the session that introduced this file).
 */

#include <stdio.h>
#include <string.h>

#include "../src/collector/breaklines.h"
#include "../src/collector/measure_points.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_widget.c / test_keyboard.c /
 * test_units.c)
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
 * Test fixture helper -- appends a point with the given name/code to a
 * store, leaving every other field zeroed (lat/lon/alt etc. are
 * irrelevant to breaklines_build(), which only ever reads ->code).
 * ========================================================================= */

static void add_point(MeasurePointStore *store, const char *name, const char *code)
{
    MeasurePoint pt;
    memset(&pt, 0, sizeof(pt));
    snprintf(pt.name, sizeof(pt.name), "%s", name);
    snprintf(pt.code, sizeof(pt.code), "%s", code);
    gm_status_t rc = measure_points_add(store, pt);
    ASSERT(rc == GM_OK, "Test fixture point was added to the store successfully");
}

/* Finds the (single, by construction in these tests) line with the
 * given key, or NULL if none exists -- a small helper so each test below
 * reads as "find the line, check its shape" rather than re-writing the
 * same linear scan in every test. */
static const Breakline *find_line(const BreaklineSet *set, const char *key)
{
    for (uint32_t i = 0; i < set->count; i++) {
        if (strcmp(set->lines[i].key, key) == 0)
            return &set->lines[i];
    }
    return NULL;
}

/* =========================================================================
 * Basic open/close: "+KEY" ... "-KEY" with no intermediate points is a
 * closed, 2-vertex line.
 * ========================================================================= */

static void test_simple_open_close(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+RCPF");
    add_point(&store, "2", "-RCPF");

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "Exactly one line is built from a simple +/- pair");
    const Breakline *line = find_line(&set, "RCPF");
    ASSERT(line != NULL, "The line is keyed by the text after the leading symbol");
    if (!line) return;
    ASSERT(line->closed, "A line with a matching '-KEY' vertex is closed");
    ASSERT(line->vertex_count == 2, "A simple +/- pair has exactly 2 vertices");
    if (line->vertex_count == 2) {
        ASSERT(line->vertex_indices[0] == 0, "First vertex is the '+' point (store index 0)");
        ASSERT(line->vertex_indices[1] == 1, "Second vertex is the '-' point (store index 1)");
    }
}

/* =========================================================================
 * Alex's own worked example, exactly as confirmed interactively:
 *   +RCPF, TREE, +CSP, RCPF, CSP, RCPF, -RCPF, -RCPF (stray), -CSP,
 *   +RCPF (reopen), -RCPF
 *
 * Expected result (see this session's design discussion for the full
 * derivation):
 *   - RCPF line #1: vertices [0, 3, 5, 6] (the '+', two plain matches,
 *     the '-') -- closed. Point 1 ("TREE") is NOT a vertex.
 *   - CSP line: vertices [2, 4, 8] -- closed, built independently and
 *     interleaved with the RCPF line above.
 *   - Point 7 ("-RCPF" with nothing open at that moment, since line #1
 *     already closed at point 6) is an ordinary unconnected point --
 *     NOT a vertex of anything.
 *   - RCPF line #2: vertices [9, 10] -- a brand-new, independent line,
 *     because reopening "+RCPF" after the first RCPF line already
 *     closed starts fresh rather than erroring or reusing the closed
 *     line.
 * ========================================================================= */

static void test_alex_worked_example(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1",  "+RCPF"); /* 0 */
    add_point(&store, "2",  "TREE");  /* 1 -- not part of any line */
    add_point(&store, "3",  "+CSP");  /* 2 */
    add_point(&store, "4",  "RCPF");  /* 3 */
    add_point(&store, "5",  "CSP");   /* 4 */
    add_point(&store, "6",  "RCPF");  /* 5 */
    add_point(&store, "7",  "-RCPF"); /* 6 -- closes RCPF line #1 */
    add_point(&store, "8",  "-RCPF"); /* 7 -- stray, nothing open -- unconnected */
    add_point(&store, "9",  "-CSP");  /* 8 -- closes CSP line */
    add_point(&store, "10", "+RCPF"); /* 9 -- reopens RCPF as a NEW line */
    add_point(&store, "11", "-RCPF"); /* 10 -- closes RCPF line #2 */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 3, "Three lines are built: RCPF#1, CSP, RCPF#2");

    /* RCPF appears twice (two independent lines with the same key) --
     * find_line() above only returns the first match, so this test
     * walks set.lines directly to identify each one by its vertex
     * count instead. */
    const Breakline *rcpf1 = NULL, *rcpf2 = NULL, *csp = NULL;
    for (uint32_t i = 0; i < set.count; i++) {
        const Breakline *l = &set.lines[i];
        if (strcmp(l->key, "CSP") == 0) {
            csp = l;
        } else if (strcmp(l->key, "RCPF") == 0) {
            if (!rcpf1) rcpf1 = l;
            else        rcpf2 = l;
        }
    }

    ASSERT(rcpf1 != NULL && rcpf2 != NULL && csp != NULL,
          "All three expected lines (RCPF#1, RCPF#2, CSP) were found");
    if (!rcpf1 || !rcpf2 || !csp) return;

    ASSERT(rcpf1->vertex_count == 4, "RCPF#1 has exactly 4 vertices (+, RCPF, RCPF, -)");
    if (rcpf1->vertex_count == 4) {
        ASSERT(rcpf1->vertex_indices[0] == 0, "RCPF#1 vertex 0 is the '+RCPF' point");
        ASSERT(rcpf1->vertex_indices[1] == 3, "RCPF#1 vertex 1 is the first plain 'RCPF' match");
        ASSERT(rcpf1->vertex_indices[2] == 5, "RCPF#1 vertex 2 is the second plain 'RCPF' match");
        ASSERT(rcpf1->vertex_indices[3] == 6, "RCPF#1 vertex 3 is the closing '-RCPF' point");
    }
    ASSERT(rcpf1->closed, "RCPF#1 is closed");

    ASSERT(csp->vertex_count == 3, "CSP has exactly 3 vertices (+, CSP, -)");
    if (csp->vertex_count == 3) {
        ASSERT(csp->vertex_indices[0] == 2, "CSP vertex 0 is the '+CSP' point");
        ASSERT(csp->vertex_indices[1] == 4, "CSP vertex 1 is the plain 'CSP' match");
        ASSERT(csp->vertex_indices[2] == 8, "CSP vertex 2 is the closing '-CSP' point");
    }
    ASSERT(csp->closed, "CSP is closed");

    ASSERT(rcpf2->vertex_count == 2, "RCPF#2 (reopened) has exactly 2 vertices (+, -)");
    if (rcpf2->vertex_count == 2) {
        ASSERT(rcpf2->vertex_indices[0] == 9, "RCPF#2 vertex 0 is the second '+RCPF' point");
        ASSERT(rcpf2->vertex_indices[1] == 10, "RCPF#2 vertex 1 is the second '-RCPF' point");
    }
    ASSERT(rcpf2->closed, "RCPF#2 is closed");

    /* Point 1 ("TREE") and point 7 (the stray "-RCPF") must not appear
     * as a vertex of ANY line -- exhaustively checked across every
     * line built, not just the ones this test already named. */
    for (uint32_t i = 0; i < set.count; i++) {
        const Breakline *l = &set.lines[i];
        for (uint32_t v = 0; v < l->vertex_count; v++) {
            ASSERT(l->vertex_indices[v] != 1,
                  "Point 1 ('TREE', unrelated code) is never a vertex of any line");
            ASSERT(l->vertex_indices[v] != 7,
                  "Point 7 (stray '-RCPF' with nothing open) is never a vertex of any line");
        }
    }
}

/* =========================================================================
 * An open line with no matching close still renders as a connected
 * polyline through whatever vertices it got -- per Alex's explicit
 * choice, there is no special "still open" decoration; the line is
 * simply unclosed in the data, which is exactly what the renderer reads
 * to decide whether to draw the line at all (it does, regardless of
 * closed state -- see measure_points_screen_draw.c's draw_map_panel()).
 * ========================================================================= */

static void test_unmatched_open_stays_open_with_its_vertices(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+CULVERT");
    add_point(&store, "2", "CULVERT");
    add_point(&store, "3", "CULVERT");
    /* No closing "-CULVERT" anywhere in the job. */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "One line is built even though it's never closed");
    const Breakline *line = find_line(&set, "CULVERT");
    ASSERT(line != NULL, "The unclosed line is still findable by its key");
    if (!line) return;
    ASSERT(!line->closed, "A line with no matching '-KEY' vertex is NOT closed");
    ASSERT(line->vertex_count == 3, "All 3 points captured while open became vertices");
}

/* =========================================================================
 * A stray "-KEY" with no currently-open KEY line is an ordinary
 * unconnected point -- Alex confirmed this explicitly, as the symmetric
 * case of a bare "KEY" with nothing open (see
 * test_bare_code_with_nothing_open_is_unconnected below).
 * ========================================================================= */

static void test_stray_close_with_nothing_open_builds_no_line(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "-RCPF"); /* nothing open -- a typo, or the '+' was never shot */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 0, "A stray '-KEY' with nothing open builds no line at all");
}

/* =========================================================================
 * A bare code (no leading symbol) that happens to match text that was
 * never opened with a '+' is just a normal unconnected point -- it does
 * NOT retroactively open a line, and does not appear as anyone's vertex.
 * ========================================================================= */

static void test_bare_code_with_nothing_open_is_unconnected(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "RCPF"); /* bare, no '+' ever seen for this key */
    add_point(&store, "2", "+RCPF"); /* opens AFTER the bare point above */
    add_point(&store, "3", "-RCPF");

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "Only one line is built (the one actually opened with '+')");
    const Breakline *line = find_line(&set, "RCPF");
    ASSERT(line != NULL, "The line opened by point 2 exists");
    if (!line) return;
    ASSERT(line->vertex_count == 2, "The line has exactly 2 vertices -- the '+' and the '-'");
    if (line->vertex_count == 2) {
        ASSERT(line->vertex_indices[0] == 1, "Vertex 0 is point 2 ('+RCPF'), not point 1");
        ASSERT(line->vertex_indices[1] == 2, "Vertex 1 is point 3 ('-RCPF')");
    }
}

/* =========================================================================
 * Closed lines cannot be added to: once "-KEY" closes a line, a later
 * bare "KEY" point does not silently reattach to the now-closed line.
 * ========================================================================= */

static void test_closed_line_does_not_accept_more_vertices(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+RCPF");
    add_point(&store, "2", "-RCPF"); /* closes it */
    add_point(&store, "3", "RCPF");  /* bare match AFTER close -- must not attach */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "Only the one (closed) line exists -- the bare point after "
                           "close did not open or join anything");
    const Breakline *line = find_line(&set, "RCPF");
    ASSERT(line != NULL, "The closed line is still findable");
    if (!line) return;
    ASSERT(line->vertex_count == 2,
          "The closed line still has exactly 2 vertices -- the post-close bare "
          "point was not appended to it");
}

/* =========================================================================
 * Multiple lines open concurrently, interleaved, each tracked
 * independently by its own key -- Alex confirmed this explicitly.
 * ========================================================================= */

static void test_multiple_concurrent_open_lines(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+A");
    add_point(&store, "2", "+B");
    add_point(&store, "3", "A");
    add_point(&store, "4", "B");
    add_point(&store, "5", "-A");
    add_point(&store, "6", "-B");

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 2, "Two independent lines are built from the interleaved A/B codes");
    const Breakline *a = find_line(&set, "A");
    const Breakline *b = find_line(&set, "B");
    ASSERT(a != NULL && b != NULL, "Both lines (A and B) are present");
    if (!a || !b) return;

    ASSERT(a->vertex_count == 3, "Line A has 3 vertices (+, plain match, -)");
    ASSERT(b->vertex_count == 3, "Line B has 3 vertices (+, plain match, -)");
    ASSERT(a->closed && b->closed, "Both lines are closed");

    if (a->vertex_count == 3)
        ASSERT(a->vertex_indices[0] == 0 && a->vertex_indices[1] == 2
              && a->vertex_indices[2] == 4,
              "Line A's vertices are exactly points 1, 3, 5 (store indices 0, 2, 4)");
    if (b->vertex_count == 3)
        ASSERT(b->vertex_indices[0] == 1 && b->vertex_indices[1] == 3
              && b->vertex_indices[2] == 5,
              "Line B's vertices are exactly points 2, 4, 6 (store indices 1, 3, 5)");
}

/* =========================================================================
 * Degenerate inputs: empty code, bare '+'/'-' with no key text, NULL
 * store -- none of these should crash or produce a bogus line.
 * ========================================================================= */

static void test_degenerate_inputs_do_not_crash_or_misbuild(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "");   /* empty code -- an ordinary unconnected point */
    add_point(&store, "2", "+");  /* bare '+' with no key text */
    add_point(&store, "3", "-");  /* bare '-' with no key text */

    BreaklineSet set;
    breaklines_build(&store, &set);
    ASSERT(set.count == 0, "Empty code and bare '+'/'-' with no key text build no lines");

    BreaklineSet set2;
    breaklines_build(NULL, &set2);
    ASSERT(set2.count == 0, "A NULL store builds an empty set, not a crash");
}

/* =========================================================================
 * An empty store builds an empty set.
 * ========================================================================= */

static void test_empty_store_builds_empty_set(void)
{
    MeasurePointStore store;
    measure_points_init(&store);

    BreaklineSet set;
    breaklines_build(&store, &set);
    ASSERT(set.count == 0, "An empty store builds zero lines");
}

/* =========================================================================
 * Determinism: building twice from the same, unmodified store produces
 * an identical result both times (see breaklines.h's doc comment on
 * this being a deliberate guarantee, not an incidental property).
 * ========================================================================= */

static void test_build_is_deterministic(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+RCPF");
    add_point(&store, "2", "RCPF");
    add_point(&store, "3", "-RCPF");

    BreaklineSet set_a, set_b;
    breaklines_build(&store, &set_a);
    breaklines_build(&store, &set_b);

    ASSERT(set_a.count == set_b.count, "Two builds from the same store agree on line count");
    if (set_a.count == set_b.count && set_a.count == 1) {
        ASSERT(strcmp(set_a.lines[0].key, set_b.lines[0].key) == 0,
              "Two builds agree on the line's key");
        ASSERT(set_a.lines[0].vertex_count == set_b.lines[0].vertex_count,
              "Two builds agree on vertex count");
        ASSERT(set_a.lines[0].closed == set_b.lines[0].closed,
              "Two builds agree on closed state");
    }
}

/* =========================================================================
 * '*' detail-suffix convention: Alex's own worked example from the
 * GeoMark request that introduced this feature -- "+GRV, GRV*4in thick,
 * GRV, -GRV*end of gravel" builds one open-then-closed GRV line with 4
 * vertices, identical to the same sequence with every '*' suffix
 * stripped. See breaklines.h's file-level doc comment for the full
 * convention.
 * ========================================================================= */

static void test_asterisk_worked_example(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+GRV");                 /* 0 -- opens GRV */
    add_point(&store, "2", "GRV*4in thick");         /* 1 -- plain match, key-truncated at '*' */
    add_point(&store, "3", "GRV");                   /* 2 -- plain match, no suffix */
    add_point(&store, "4", "-GRV*end of gravel");    /* 3 -- closes GRV, key-truncated at '*' */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "Exactly one line is built, matching '+GRV'/'GRV'/'-GRV' with no '*'");
    const Breakline *line = find_line(&set, "GRV");
    ASSERT(line != NULL, "The line is keyed by \"GRV\" -- the '*' suffix never reaches the key");
    if (!line) return;
    ASSERT(line->closed, "The line closes on the '-GRV*end of gravel' vertex");
    ASSERT(line->vertex_count == 4, "All 4 points are vertices -- '*' suffixes don't break the line");
    if (line->vertex_count == 4) {
        ASSERT(line->vertex_indices[0] == 0, "Vertex 0 is the '+GRV' point");
        ASSERT(line->vertex_indices[1] == 1, "Vertex 1 is 'GRV*4in thick', matched despite its suffix");
        ASSERT(line->vertex_indices[2] == 2, "Vertex 2 is the plain 'GRV' point");
        ASSERT(line->vertex_indices[3] == 3, "Vertex 3 is '-GRV*end of gravel', matched despite its suffix");
    }
}

/* =========================================================================
 * The full suffix text (including the '*' itself) is never touched by
 * breaklines_build() -- it only reads store->points[i].code, never
 * writes it (see breaklines.h's determinism guarantee), so the original
 * MeasurePoint the store holds still has the complete text for export.
 * ========================================================================= */

static void test_asterisk_suffix_is_not_stripped_from_the_store(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+GRV");
    add_point(&store, "2", "GRV*4in thick");

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(strcmp(store.points[1].code, "GRV*4in thick") == 0,
          "The store's own point 2 code is untouched -- full text, "
          "asterisk and suffix included, still reaches export");
}

/* =========================================================================
 * A code opening with '+' whose key is entirely suffix (e.g. "+*note",
 * no real code text before the '*') has nothing to key a line by --
 * treated the same as a bare "+" with no key text at all: an ordinary
 * unconnected point, not a crash or a zero-length-keyed line.
 * ========================================================================= */

static void test_asterisk_only_key_is_unconnected(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+*note only, no real code");

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 0, "A '+' with an all-'*'-suffix key builds no line at all");
}

/* =========================================================================
 * '*' does not create false matches between two genuinely different
 * codes that merely share a prefix -- "GRVL*compacted" must NOT match a
 * line keyed "GRV". find_open_line()'s length-checked comparison (not a
 * bare prefix strncmp()) is what this test actually exercises.
 * ========================================================================= */

static void test_asterisk_does_not_cause_false_prefix_match(void)
{
    MeasurePointStore store;
    measure_points_init(&store);
    add_point(&store, "1", "+GRV");             /* 0 -- opens a line keyed "GRV" */
    add_point(&store, "2", "GRVL*compacted");   /* 1 -- different code text ("GRVL"), must NOT join */
    add_point(&store, "3", "-GRV");             /* 2 -- closes the "GRV" line */

    BreaklineSet set;
    breaklines_build(&store, &set);

    ASSERT(set.count == 1, "Only the one \"GRV\" line is built");
    const Breakline *line = find_line(&set, "GRV");
    ASSERT(line != NULL, "The \"GRV\" line exists");
    if (!line) return;
    ASSERT(line->vertex_count == 2,
          "\"GRV\" has exactly 2 vertices (+, -) -- the \"GRVL*compacted\" "
          "point (different key text) is NOT one of them");
    if (line->vertex_count == 2) {
        ASSERT(line->vertex_indices[0] == 0, "Vertex 0 is the '+GRV' point");
        ASSERT(line->vertex_indices[1] == 2, "Vertex 1 is the '-GRV' point, not point 1 ('GRVL*compacted')");
    }
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_simple_open_close();
    test_alex_worked_example();
    test_unmatched_open_stays_open_with_its_vertices();
    test_stray_close_with_nothing_open_builds_no_line();
    test_bare_code_with_nothing_open_is_unconnected();
    test_closed_line_does_not_accept_more_vertices();
    test_multiple_concurrent_open_lines();
    test_degenerate_inputs_do_not_crash_or_misbuild();
    test_empty_store_builds_empty_set();
    test_build_is_deterministic();
    test_asterisk_worked_example();
    test_asterisk_suffix_is_not_stripped_from_the_store();
    test_asterisk_only_key_is_unconnected();
    test_asterisk_does_not_cause_false_prefix_match();

    if (g_tests_failed == 0) {
        printf("All %d breakline tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d breakline tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}