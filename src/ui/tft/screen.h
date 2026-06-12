/**
 * @file screen.h
 * @brief RTK status screen layout for the rover TFT display.
 *
 * screen_update() takes a snapshot of the current position and renders
 * the full RTK status screen.  It is called from the rover station loop
 * at ~2 Hz — no internal state is shared with the collector threads.
 *
 * Screen layout (480x320, landscape):
 *
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │  [FIX BADGE]  GeoMark Rover                                         │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  LAT   47.12345678°                                                  │
 *   │  LON  -96.12345678°                                                  │
 *   │  ALT   892.4 ft                                                      │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  HDOP  1.2          SATS  09          AGE  3s                       │
 *   └─────────────────────────────────────────────────────────────────────┘
 */

#ifndef GEOMARK_SCREEN_H
#define GEOMARK_SCREEN_H

#include "geomark.h"
#include <stdbool.h>

/**
 * @brief Render the full RTK status screen from a position snapshot.
 *
 * Redraws only the regions that change — fix badge and data fields.
 * First call also draws the static chrome (labels, dividers).
 *
 * Must be called after display_open() succeeds.
 *
 * @param pos    Current position snapshot (copied from RoverFixState).
 * @param valid  True if at least one fix has been received.
 * @param now_ms Current monotonic time in ms (for age-of-fix calculation).
 */
void screen_update(const gm_position_t *pos, bool valid, uint32_t now_ms);

/**
 * @brief Draw the initial static chrome (labels, dividers, title).
 *
 * Called once on startup before the first screen_update().
 */
void screen_init(void);

#endif /* GEOMARK_SCREEN_H */