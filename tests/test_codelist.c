/**
 * @file tests/test_codelist.c
 * @brief Unit tests for survey/codelist.h's built-in default code list --
 *        specifically the replacement of the original 12-entry hand-picked
 *        list with the full ACG_TOPO_V1.xlsx point+line code list (280
 *        entries) Alex supplied. Exercises codelist_load()'s fallback
 *        path only (no point_codes.txt present in this disposable test
 *        environment, so it always resolves to s_defaults[] -- same
 *        environment assumption test_screens.c's own code-picker test
 *        already relies on).
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "survey/codelist.h"

/* =========================================================================
 * Minimal test harness (matches tests/test_breaklines.c / test_keyboard.c)
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
 * Exact count: 154 point codes + 126 line codes from ACG_TOPO_V1.xlsx's
 * KLJ_TOPO_V4_Beta sheet, with the Action-section codes (!, JTP, PC, PT)
 * Alex explicitly asked to disregard, and the bare "+"/"-" Action-section
 * entries (already GeoMark's own breakline start/end prefix convention,
 * not standalone point codes -- see collector/breaklines.h) also excluded.
 * ========================================================================= */

static void test_default_count_is_280(void)
{
    CodeList list;
    codelist_load(&list);

    ASSERT(list.count == 280,
          "The built-in default list has exactly 280 entries "
          "(154 point codes + 126 line codes)");
    ASSERT(!list.from_file,
          "No point_codes.txt exists in this test environment -- "
          "codelist_load() falls back to the built-in defaults");
}

/* =========================================================================
 * Spot-check a handful of codes across both sections of the source
 * spreadsheet, plus the exact codes from Alex's own worked example for
 * the '*' detail-suffix feature (GRV, used in measure_points_screen's
 * own '*' tests too).
 * ========================================================================= */

static void test_spot_check_known_codes_present(void)
{
    CodeList list;
    codelist_load(&list);

    const struct { const char *code; const char *desc; } cases[] = {
        { "CP",     "Control Point" },          /* point code, heavily used in Alex's own jobs */
        { "GRV",    "Gravel" },                  /* point code -- '*' worked example uses this */
        { "CORSEC", "Section Corner" },          /* point code, longer code text */
        { "BM",     "Benchmark" },               /* point code */
        { "WING",   "Wing Wall" },               /* line code */
        { "GDR",    "Guard Rail" },              /* line code, appears in Alex's own export example */
        { "RCP",    "Reinforced Conc. Pipe" },   /* line code */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const CodeEntry *e = codelist_find(&list, cases[i].code);
        ASSERT(e != NULL, "Expected code is present in the built-in default list");
        if (e)
            ASSERT(strcmp(e->desc, cases[i].desc) == 0,
                  "Expected code's description matches ACG_TOPO_V1.xlsx exactly");
    }
}

/* =========================================================================
 * The four Action-section codes Alex explicitly asked to disregard
 * (!, JTP, PC, PT) must NOT appear anywhere in the default list --
 * neither as their own entry nor accidentally shadowed by a real code.
 * '+' and '-' (the other two Action-section entries) are also excluded,
 * since those are GeoMark's own existing breakline prefix convention,
 * not standalone point codes.
 * ========================================================================= */

static void test_excluded_action_codes_absent(void)
{
    CodeList list;
    codelist_load(&list);

    const char *excluded[] = { "!", "JTP", "PC", "PT", "+", "-" };
    for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); i++) {
        ASSERT(codelist_find(&list, excluded[i]) == NULL,
              "Excluded Action-section code is absent from the default list");
    }
}

/* =========================================================================
 * No duplicate codes -- codelist_find() returns the FIRST match, so a
 * silent duplicate would make the second entry permanently unreachable
 * from the picker. Exhaustive O(n^2) check; 280 entries is small enough
 * that this costs nothing in a test run.
 * ========================================================================= */

static void test_no_duplicate_codes(void)
{
    CodeList list;
    codelist_load(&list);

    for (uint32_t i = 0; i < list.count; i++) {
        for (uint32_t j = i + 1; j < list.count; j++) {
            ASSERT(strcasecmp(list.entries[i].code, list.entries[j].code) != 0,
                  "No two entries in the default list share the same code "
                  "(codelist_find()'s first-match semantics would strand "
                  "the second one)");
        }
    }
}

/* =========================================================================
 * Every code and description fits its buffer with room for the NUL
 * terminator, and every code was upper-cased on load (parse_line()'s
 * contract) -- the source spreadsheet already ships upper-case codes,
 * but this guards against a future edit to s_defaults[] introducing a
 * lower-case one, since load_defaults() (unlike try_load()'s file path)
 * does NOT re-run parse_line()'s uppercasing step.
 * ========================================================================= */

static void test_entries_fit_buffers_and_are_uppercase(void)
{
    CodeList list;
    codelist_load(&list);

    for (uint32_t i = 0; i < list.count; i++) {
        const CodeEntry *e = &list.entries[i];
        ASSERT(strlen(e->code) < sizeof(e->code),
              "Code fits within its buffer with room for the NUL terminator");
        ASSERT(strlen(e->desc) < sizeof(e->desc),
              "Description fits within its buffer with room for the NUL terminator");
        ASSERT(e->code[0] != '\0', "No entry has an empty code");

        for (const char *c = e->code; *c; c++) {
            ASSERT(!(*c >= 'a' && *c <= 'z'),
                  "Every code is upper-case (matches codelist_load()'s "
                  "file-path contract, kept consistent for the built-in "
                  "defaults too)");
        }
    }
}

/* =========================================================================
 * codelist_get() index bounds -- valid indices return the right entry
 * in insertion order, one past the end returns NULL.
 * ========================================================================= */

static void test_get_bounds(void)
{
    CodeList list;
    codelist_load(&list);

    const CodeEntry *first = codelist_get(&list, 0);
    ASSERT(first != NULL, "Index 0 returns the first entry");
    if (first)
        ASSERT(strcmp(first->code, "AC") == 0,
              "First entry is \"AC\" (Air Conditioner), the first row of "
              "ACG_TOPO_V1.xlsx's Point codes section");

    ASSERT(codelist_get(&list, list.count) == NULL,
          "One index past the last valid entry returns NULL");
    ASSERT(codelist_get(&list, list.count + 100) == NULL,
          "A far out-of-range index returns NULL, not garbage");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    test_default_count_is_280();
    test_spot_check_known_codes_present();
    test_excluded_action_codes_absent();
    test_no_duplicate_codes();
    test_entries_fit_buffers_and_are_uppercase();
    test_get_bounds();

    if (g_tests_failed == 0) {
        printf("All %d codelist tests passed.\n", g_tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d/%d codelist tests FAILED.\n", g_tests_failed, g_tests_run);
        return 1;
    }
}