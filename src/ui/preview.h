/**
 * @file ui/preview.h
 * @brief Entry point for the screen-stack UI (Sleep -> Main Menu ->
 *        Job Create/Open -> Measure Points, etc.) on real display
 *        hardware. As of the geomark-ui.service switch, this is the
 *        UI geomark-ui.service launches by default -- ui/client.c's
 *        legacy button-only survey flow remains in the codebase and
 *        runnable (geomark --mode ui --host <ip>, omitting
 *        --ui-preview), but is no longer what the running service
 *        executes. The "preview" name predates that switch; left as-is
 *        for now rather than bundled into this change as a rename
 *        (would touch every #include site and the CMakeLists entry).
 *
 * geomark-ui.service's ExecStart:
 *   geomark --mode ui --host 192.168.10.1 --ui-preview
 *
 * Manual/SSH invocation (e.g. for testing) uses the same flag:
 *   geomark --mode ui --ui-preview [--host <ip>]
 *
 * Controls (touch-only -- the physical GPIO d-pad is no longer read by
 * this UI; see this header's own git history for the prior button-based
 * design if ever needed again):
 *   Tap      — hit-tests against the focused screen's widgets, relocates
 *              focus to the tapped widget, then activates it (see
 *              ui/core/widget.c's ui_grid_handle_event()).
 *   < Back   — every screen has a fixed top-left "< Back" button (see
 *              ui/core/widget.h's ui_grid_add_back_button() doc comment)
 *              that dispatches the same UI_EVENT_BACK the old physical
 *              Left button used to -- including Measure Points' "close
 *              an open overlay first" behavior, unchanged.
 *   Ctrl+C   — exit
 *
 * The legacy ui/client.c flow is unaffected by this -- it still reads
 * the physical GPIO d-pad via ui/gpio_button.h for its own button-only
 * survey screen, since that file is untouched per the project's
 * production-flow rule. Only this screen-stack UI's input loop dropped
 * GPIO polling.
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