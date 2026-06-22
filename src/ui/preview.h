/**
 * @file ui/preview.h
 * @brief Manual, opt-in entry point for previewing the new screen-stack UI
 *        (Sleep -> Main Menu -> placeholder screens) on real display
 *        hardware, without touching ui/client.c's production flow.
 *
 * Launch manually over SSH — geomark-ui.service does NOT use this path:
 *   geomark --mode ui --ui-preview [--host <ip>]
 *
 * Controls (GPIO d-pad and capacitive touch both active):
 *   Up/Down  — move focus
 *   Center   — activate the focused widget
 *   Left     — back (matches the existing project-wide "Left = back"
 *              convention; no current screen needs Left for horizontal
 *              navigation yet — see preview.c's translate_input())
 *   Right    — reserved, currently a no-op
 *   Tap      — hit-tests against the focused screen's widgets, relocates
 *              focus to the tapped widget, then activates it (see
 *              ui/core/widget.c's ui_grid_handle_event()). Falls back to
 *              button-only if no capacitive touch device is found.
 *   Ctrl+C   — exit
 *
 * RTK feed: pole_top_host is the rover's IP/hostname (same --host flag and
 * default main.c already uses for ui_client_run()), passed straight
 * through to net/rtk_feed_client.h to back Measure Points' live fix --
 * see that header for why this is a separate module rather than
 * networking logic inlined here. ui_preview_run() owns the RtkFeedClient's
 * start/stop lifecycle internally; callers don't manage it.
 */

#ifndef GEOMARK_UI_PREVIEW_H
#define GEOMARK_UI_PREVIEW_H

#include "geomark.h"

gm_status_t ui_preview_run(const char *pole_top_host);

#endif /* GEOMARK_UI_PREVIEW_H */