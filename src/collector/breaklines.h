/**
 * @file collector/breaklines.h
 * @brief Breakline (connected-line) interpretation of MeasurePoint::code,
 *        for the Measure Points map panel.
 *
 * Survey field-book convention (per the agency CADD editing standards
 * Alex supplied -- "Survey Field Book Codes and Descriptions"): a code
 * beginning with '+' starts a line, a code beginning with '-' ends it,
 * and the line connects every point sharing that code's text (after the
 * leading symbol) captured while the line is open. This module is the
 * first real interpretation of MeasurePoint::code -- see
 * measure_points.h's file-level doc comment, which deliberately left
 * code[] uninterpreted pending exactly this.
 *
 * Algorithm (confirmed against Alex's own worked examples before being
 * written down here -- see the session that introduced this file):
 *
 *   - A code of "+KEY" OPENS a line keyed by KEY (the text after the
 *     leading '+'), if no line with that exact key is currently open,
 *     and becomes that line's first vertex.
 *   - A code that is EXACTLY "KEY" (no leading symbol) while a line
 *     keyed KEY is open becomes that line's next vertex, in capture
 *     order.
 *   - A code of "-KEY" becomes that line's FINAL vertex and CLOSES it.
 *     Once closed, that exact key can never be reopened by reusing the
 *     same still-open line -- but seeing another "+KEY" later starts a
 *     brand-new, independent line with the same key text (Alex
 *     confirmed key text is reusable across separate lines within one
 *     job; only an actually-open line is protected from being added to
 *     after its own close).
 *   - A point whose code doesn't match the key of any currently-open
 *     line (different text, blank, or a +/-KEY for some other key) is
 *     an ordinary unconnected point -- it has no effect on any open
 *     line and isn't a vertex of one (Alex confirmed: a "TREE" shot
 *     between two RCPF line vertices doesn't break the RCPF line, it's
 *     just its own separate spot shot).
 *   - Multiple lines may be open at once, tracked independently by key
 *     (Alex confirmed interleaved +RCPF / +CSP capture is valid).
 *   - A "-KEY" with no currently-open KEY line (typo, or the matching
 *     '+' was never shot) is just an ordinary unconnected point -- same
 *     treatment as the symmetric case of a bare "KEY" with nothing open
 *     (Alex confirmed both read the same way: no open line exists to
 *     join, so the point stands alone).
 *   - A '*' anywhere in a code marks the start of a free-text detail
 *     suffix that carries information for the drafter/engineer but has
 *     no effect on line-key matching: everything from '*' onward
 *     (asterisk included) is stripped before a code is classified or
 *     compared. "GRV" and "GRV*4in thick" key-match identically, so a
 *     surveyor can append detail to any capture -- '+', '-', or plain --
 *     without breaking the line it belongs to. Alex's own worked
 *     example: "+GRV, GRV*4in thick, GRV, -GRV*end of gravel" builds one
 *     open-then-closed GRV line with 4 vertices, exactly as if every
 *     '*' suffix were absent. The suffix text itself is NOT discarded --
 *     MeasurePoint::code keeps it verbatim (breaklines_build() never
 *     modifies the store, see this header's determinism guarantee
 *     below), so it still reaches CSV/LandXML export in the
 *     Description/code column for the drafter to read. A code that is
 *     ALL suffix (starts with '*', e.g. a bare "*just a note") has no
 *     real code text before the '*' to key a line by, so it is treated
 *     exactly like an empty code -- an ordinary unconnected point, not
 *     an error.
 *
 * This module is pure logic over a MeasurePointStore -- no
 * ui/tft/display.h dependency, same logic/render split every other
 * screen module in this codebase already follows (see
 * measure_points_screen.h vs. measure_points_screen_draw.c). The result
 * (BreaklineSet) is recomputed fresh from the store each time the map
 * panel renders rather than maintained incrementally -- GM_MEASURE_POINTS_MAX
 * (4096) points is a small enough linear scan to redo every frame with
 * no real cost, and "recompute from the source of truth" avoids an
 * entire class of incremental-update bugs (a point edited or a CSV
 * reloaded out from under stale incremental state) for a screen that
 * already reloads its whole store on every on_enter (see
 * measure_points_screen.c's reload_job_data()).
 */

#ifndef GEOMARK_BREAKLINES_H
#define GEOMARK_BREAKLINES_H

#include <stdbool.h>
#include <stdint.h>

#include "collector/measure_points.h"
#include "geomark.h"

/* -------------------------------------------------------------------------
 * Limits
 * ---------------------------------------------------------------------- */

/** Max distinct lines (open + closed) tracked per build. Generous
 *  relative to any real field book's per-job culvert/line count -- raise
 *  if a real job ever approaches this, same "raise on real demand, not
 *  estimate" precedent GM_MEASURE_POINTS_MAX itself documents. */
#define GM_BREAKLINES_MAX 256

/** Max vertices in a single line. Deliberately NOT GM_MEASURE_POINTS_MAX
 *  (4096) -- a per-line cap that size would make sizeof(Breakline) (and
 *  so sizeof(BreaklineSet), which holds GM_BREAKLINES_MAX of them)
 *  balloon into multiple megabytes, unsafe to keep as a stack local
 *  (exactly the failure this comment is here to prevent a future editor
 *  from reintroducing -- confirmed by an actual stack-overflow crash
 *  during this feature's own test development before this constant was
 *  lowered). No real field-book line realistically has anywhere close
 *  to this many vertices -- a culvert/curb/ditch line is normally tens
 *  of shots at most. Raise only if a real job's line count ever
 *  approaches this, same "raise on demonstrated need, not estimate"
 *  precedent GM_MEASURE_POINTS_MAX itself documents -- but raising this
 *  one has the BreaklineSet-size consequence above to weigh against
 *  that, which GM_MEASURE_POINTS_MAX itself does not have. */
#define GM_BREAKLINE_MAX_VERTICES 256

/* -------------------------------------------------------------------------
 * A single line
 * ---------------------------------------------------------------------- */

typedef struct {
    /* Indices into the MeasurePointStore this line was built from,
     * in capture order -- NOT a copy of the points themselves, so a
     * line is only ever as current as the store it was built from
     * (see this header's file-level doc comment on why that's the
     * intended lifetime: rebuilt fresh every render, never cached
     * across a store mutation). */
    uint32_t vertex_indices[GM_BREAKLINE_MAX_VERTICES];
    uint32_t vertex_count;

    /* The key text after the leading '+'/'-', with any '*' detail
     * suffix (and the '*' itself) already stripped -- e.g. "24RCPF" for
     * a code of "+24RCPF", and also "GRV" for a code of "+GRV*top of
     * ditch" (see this header's file-level doc comment on the '*'
     * convention). Truncated to this buffer's capacity on an
     * over-length code, same MEASURE_POINT_CODE_MAX-derived sizing
     * every other code-text copy in this codebase already uses. */
    char key[GM_MEASURE_POINT_CODE_MAX];

    bool closed; /* true once a matching "-KEY" vertex has been added */
} Breakline;

/* -------------------------------------------------------------------------
 * The full set built from one store
 * ---------------------------------------------------------------------- */

typedef struct {
    Breakline lines[GM_BREAKLINES_MAX];
    uint32_t count;
} BreaklineSet;

/**
 * Builds the full set of lines (open and closed) from every point in
 * store, in store order (store order IS capture order -- see
 * measure_points_add()'s own doc comment: point_num is assigned
 * sequentially as each point is appended, never reordered).
 *
 * Deterministic and side-effect-free: calling this twice on the same
 * store produces an identical BreaklineSet both times, and the store
 * itself is never modified. See this header's file-level doc comment
 * for the full algorithm this implements.
 *
 * If more than GM_BREAKLINES_MAX distinct lines would be needed, the
 * lines beyond that cap are silently dropped (their vertices become
 * ordinary unconnected points for rendering purposes) -- logged once
 * per build via util/log.h, same "cap is hit, log it, degrade
 * gracefully" precedent measure_points_add()'s own GM_ERR_NOMEM path
 * establishes for its own fixed-capacity store.
 */
void breaklines_build(const MeasurePointStore *store, BreaklineSet *out);

#endif /* GEOMARK_BREAKLINES_H */