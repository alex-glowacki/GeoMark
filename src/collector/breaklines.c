/**
 * @file collector/breaklines.c
 * @brief Implementation -- see breaklines.h for the algorithm and design
 *        rationale.
 */

#define _GNU_SOURCE

#include "collector/breaklines.h"

#include <stdio.h>
#include <string.h>

#include "util/log.h"

/* -------------------------------------------------------------------------
 * Code parsing
 *
 * A code's first byte determines its kind; everything after it is the
 * key text (copied as-is, NOT re-validated here -- the keyboard's own
 * closed character set, see ui/core/keyboard.h, already guarantees code[]
 * can never contain anything this module needs to reject).
 * ---------------------------------------------------------------------- */

typedef enum {
    CODE_KIND_PLAIN = 0, /* no leading '+'/'-', or an empty code */
    CODE_KIND_OPEN,      /* leading '+' */
    CODE_KIND_CLOSE,     /* leading '-' */
} CodeKind;

static CodeKind classify_code(const char *code, const char **key_out)
{
    if (!code || code[0] == '\0') {
        *key_out = "";
        return CODE_KIND_PLAIN;
    }

    if (code[0] == '+') {
        *key_out = code + 1;
        return CODE_KIND_OPEN;
    }

    if (code[0] == '-') {
        *key_out = code + 1;
        return CODE_KIND_CLOSE;
    }

    *key_out = code;
    return CODE_KIND_PLAIN;
}

/* -------------------------------------------------------------------------
 * Open-line lookup
 *
 * Linear scan over the lines built so far -- GM_BREAKLINES_MAX (256) is
 * small enough that this costs nothing in practice, and a job realistically
 * has at most a handful of lines open at any one moment regardless of how
 * many have been built/closed over the whole session (see breaklines.h's
 * file-level doc comment on why a fresh O(n) rebuild every render is the
 * right tradeoff here, not a hash table).
 * ---------------------------------------------------------------------- */

static int32_t find_open_line(const BreaklineSet *set, const char *key)
{
    if (key[0] == '\0')
        return -1; /* an empty key can never match -- "+"<nothing> and a
                    * bare empty code are both degenerate inputs no real
                    * field-book code would produce; treating them as
                    * "never matches" keeps this function's contract
                    * simple rather than special-casing a key nobody
                    * will actually type */

    for (uint32_t i = 0; i < set->count; i++) {
        if (!set->lines[i].closed && strcmp(set->lines[i].key, key) == 0)
            return (int32_t)i;
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * Build
 * ---------------------------------------------------------------------- */

void breaklines_build(const MeasurePointStore *store, BreaklineSet *out)
{
    memset(out, 0, sizeof(*out));
    if (!store)
        return;

    bool warned_lines_full = false;
    /* One warned-flag per line slot -- GM_BREAKLINES_MAX (256) bools is
     * a trivial stack footprint, nothing like the BreaklineSet sizing
     * concern GM_BREAKLINE_MAX_VERTICES's own doc comment addresses.
     * Logged once per line that actually hits its vertex cap, same
     * "warn once, not once per dropped vertex" convention the line-cap
     * warning above already follows. */
    bool warned_vertices_full[GM_BREAKLINES_MAX] = {0};

    for (uint32_t i = 0; i < store->count; i++) {
        const char *key;
        CodeKind kind = classify_code(store->points[i].code, &key);

        if (kind == CODE_KIND_PLAIN) {
            /* Only relevant if it exactly matches an open line's key --
             * a "+RCPF" line stays open and this becomes its next
             * vertex. Anything else (different text, or empty) is an
             * ordinary unconnected point and is simply not added to any
             * line -- see this header's file-level doc comment on the
             * "TREE in the middle of an RCPF line" case Alex confirmed. */
            if (key[0] == '\0')
                continue;

            int32_t idx = find_open_line(out, key);
            if (idx < 0)
                continue;

            Breakline *line = &out->lines[idx];
            if (line->vertex_count < GM_BREAKLINE_MAX_VERTICES) {
                line->vertex_indices[line->vertex_count] = i;
                line->vertex_count++;
            } else if (!warned_vertices_full[idx]) {
                log_warn("breaklines_build: line '%s' hit the %u-vertex cap -- "
                         "further matching points become unconnected",
                         line->key, GM_BREAKLINE_MAX_VERTICES);
                warned_vertices_full[idx] = true;
            }
            continue;
        }

        if (kind == CODE_KIND_OPEN) {
            if (key[0] == '\0')
                continue; /* a bare "+" with no key text -- nothing to key
                           * a line by; treated as a plain point (no key
                           * to assign) rather than crashing or guessing */

            /* Opening with a key that already has an OPEN line is not
             * itself rejected -- find_open_line() above would have
             * already matched a still-open same-key line for a PLAIN
             * code, but here we're looking at another "+KEY". Per
             * breaklines.h's algorithm, a still-open line can't be
             * reopened by definition (it's already open), so this
             * always starts a genuinely new line -- closed lines with
             * the same key are independent and unaffected, which is
             * exactly Alex's "codes are reusable after close" rule. */
            if (out->count >= GM_BREAKLINES_MAX) {
                if (!warned_lines_full) {
                    log_warn("breaklines_build: line cap (%u) reached -- "
                             "further '+' codes become unconnected points",
                             GM_BREAKLINES_MAX);
                    warned_lines_full = true;
                }
                continue;
            }

            Breakline *line = &out->lines[out->count];
            memset(line, 0, sizeof(*line));
            snprintf(line->key, sizeof(line->key), "%s", key);
            line->vertex_indices[0] = i;
            line->vertex_count = 1;
            line->closed = false;
            out->count++;
            continue;
        }

        /* CODE_KIND_CLOSE */
        if (key[0] == '\0')
            continue; /* bare "-" with no key text -- same non-key-able
                       * case as a bare "+" above */

        int32_t idx = find_open_line(out, key);
        if (idx < 0)
            continue; /* a "-KEY" with nothing open by that key is just
                       * an ordinary unconnected point -- Alex confirmed
                       * this symmetric case explicitly */

        Breakline *line = &out->lines[idx];
        if (line->vertex_count < GM_BREAKLINE_MAX_VERTICES) {
            line->vertex_indices[line->vertex_count] = i;
            line->vertex_count++;
        } else if (!warned_vertices_full[idx]) {
            log_warn("breaklines_build: line '%s' hit the %u-vertex cap -- "
                     "its closing point was dropped", line->key, GM_BREAKLINE_MAX_VERTICES);
            warned_vertices_full[idx] = true;
        }
        line->closed = true;
    }
}