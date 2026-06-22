/**
 * @file net/rtk_feed_client.h
 * @brief stream_client-backed implementation of measure_points_screen.h's
 *        RtkFeed seam -- the real-hardware counterpart to
 *        measure_points_no_feed().
 *
 * Wires together, mirroring ui/client.c's own UISharedState / packet_callback
 * / snapshot-under-lock shape exactly (see that file and
 * measure_points_screen.h's RtkFeed doc comment, which calls out this same
 * pattern as the one a real implementation should follow):
 *
 *   stream_client  ->  packet callback (recv thread)  ->  RtkFeedClient
 *                                                          (mutex-protected)
 *   measure_points_screen's on_tick  ->  feed.fn()  ->  snapshot under lock
 *
 * This is a separate, reusable module rather than logic written directly
 * into ui/preview.c, for the same reason measure_points_screen.h itself
 * gives for the RtkFeed seam in the first place: networking and UI-tick
 * logic are different concerns, and keeping the adapter in its own module
 * means it could be unit-tested independently of any screen (though no such
 * test exists yet -- see test_screens.c's own test_feed_fn()/TestFeedState,
 * which exercises the *screen's* use of an RtkFeed without any networking,
 * by design).
 *
 * Deliberately does NOT reuse or modify ui/client.c's UISharedState --
 * that struct is private to the legacy survey-capture flow, which must
 * stay completely untouched (project hard rule). This module duplicates
 * the same small mutex-snapshot shape rather than extracting a shared
 * helper out of client.c, since touching that file at all is off the table
 * regardless of how small the change would be.
 *
 * Connection-staleness handling: stream_client.h's StreamClientCallback is
 * only ever invoked on a successfully received packet -- there is no
 * "disconnected" callback, and RoverStatusPacket::age_of_fix_s describes
 * the age of the GNSS fix itself (rover-side), not how long it has been
 * since the handheld last heard from the rover. So this module tracks its
 * own "time since last packet" using a monotonic clock (see
 * monotonic_ms() below, the same private-per-module pattern every other
 * timing-dependent file in this codebase already uses -- ui/preview.c,
 * ui/client.c, ui/gpio_button.c, ui/core/touch_input.c, rover/station.c --
 * rather than introducing a new shared time helper for this one case) and
 * reports RtkFeedPosition::valid = false once nothing has arrived for
 * RTK_FEED_CLIENT_STALE_MS, even though stream_client's own auto-reconnect
 * means the socket-level disconnect is otherwise invisible to this module.
 * This is a deliberate improvement over literally copying
 * ui/client.c::packet_callback(), which sets valid=1 on the first packet
 * and never clears it again -- that behavior is fine for client.c's own
 * purposes (out of scope to change here) but wrong for this module, since
 * measure_points_screen.h's own doc comment defines RtkFeedPosition::valid
 * as "false until the feed has a real fix" -- a real-time property, not
 * an ever-had-a-fix one.
 */

#ifndef GEOMARK_NET_RTK_FEED_CLIENT_H
#define GEOMARK_NET_RTK_FEED_CLIENT_H

#include <pthread.h>
#include <stdint.h>

#include "geomark.h"
#include "ui/screens/measure_points_screen.h"

/** How long without a successfully received packet before the feed
 *  reports valid=false, even if the last packet ever received reported a
 *  good fix. stream_client streams at 2 Hz (net/stream_client.h) and
 *  reconnects every 2s on drop (same file) -- 3000ms is a few missed
 *  packets' worth of slack, generous enough not to flicker on ordinary
 *  jitter but short enough that a real disconnect shows up within one
 *  reconnect cycle. */
#define RTK_FEED_CLIENT_STALE_MS 3000u

/**
 * Shared state written by the stream_client receive thread (via
 * packet_callback(), rtk_feed_client.c) and read by rtk_feed_client_fn()
 * on the UI thread once per tick -- same split client.c's UISharedState
 * already has, see this header's file-level doc comment for why it isn't
 * reused directly.
 */
typedef struct {
    pthread_mutex_t mutex;
    RtkFeedPosition latest;  /* last successfully parsed packet, translated
                              * to RtkFeedPosition's shape */
    uint32_t last_packet_ms; /* monotonic_ms() at the last packet received;
                              * 0 means "never" */
    int started;             /* true once rtk_feed_client_start() has
                              * succeeded -- guards rtk_feed_client_stop()
                              * against double-stop / stop-without-start */
} RtkFeedClient;

/**
 * Starts the underlying stream_client receive thread, connecting to host
 * (see net/stream_client.h -- same auto-reconnect-every-2s behavior, this
 * module adds no retry logic of its own beyond what stream_client already
 * does). client must remain valid until rtk_feed_client_stop() is called;
 * a stack-allocated RtkFeedClient living for the lifetime of ui_preview_run()
 * is the expected usage, matching how ui/client.c's UISharedState lives on
 * that file's own call stack.
 *
 * Returns GM_OK on success, GM_ERR_IO if the receive thread could not be
 * started (matches stream_client_start()'s own contract).
 */
gm_status_t rtk_feed_client_start(RtkFeedClient *client, const char *host);

/**
 * Stops the receive thread and closes the socket. Safe to call even if
 * rtk_feed_client_start() was never called or failed (no-op in that case).
 */
void rtk_feed_client_stop(RtkFeedClient *client);

/**
 * Returns an RtkFeed bound to client, suitable for passing directly to
 * measure_points_screen_init() in place of measure_points_no_feed(). client
 * must already have been started via rtk_feed_client_start() and must
 * outlive the screen -- same not-owned, copied-by-value contract
 * measure_points_screen.h's RtkFeed doc comment already establishes for
 * every RtkFeed.
 */
RtkFeed rtk_feed_client_as_feed(RtkFeedClient *client);

#endif /* GEOMARK_NET_RTK_FEED_CLIENT_H */