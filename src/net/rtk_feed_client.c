/**
 * @file net/rtk_feed_client.c
 * @brief See net/rtk_feed_client.h.
 */

#define _GNU_SOURCE

#include "net/rtk_feed_client.h"

#include <string.h>
#include <time.h>

#include "net/stream_client.h"
#include "util/log.h"

/* --------------------------------------------------------------------------
 * Monotonic clock helper -- private to this module, same per-file pattern
 * every other timing-dependent module in this codebase already uses (see
 * this header's file-level doc comment for the full list).
 * ----------------------------------------------------------------------- */

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* --------------------------------------------------------------------------
 * Packet callback -- runs on stream_client's receive thread
 * ----------------------------------------------------------------------- */

static void packet_callback(const RoverStatusPacket *pkt, void *user)
{
    RtkFeedClient *client = (RtkFeedClient *)user;

    /* A packet with valid=0 still means "connected, but no usable fix
     * yet" (e.g. rover hasn't acquired satellites) -- still worth
     * recording as "we heard from the rover just now" so the staleness
     * timeout doesn't fire while the rover is alive and simply fixless,
     * but the position fields themselves are meaningless per
     * rover_packet.h's own doc comment, so latest.valid must follow
     * pkt->valid, not the fact that a packet arrived at all. */
    RtkFeedPosition pos;
    memset(&pos, 0, sizeof(pos));
    pos.valid = (pkt->valid != 0);
    if (pos.valid) {
        pos.lat         = pkt->lat;
        pos.lon         = pkt->lon;
        pos.alt         = pkt->alt_msl;
        pos.hdop        = pkt->hdop;
        pos.num_sats    = pkt->num_sats;
        pos.fix_quality = pkt->fix_quality;
    }

    pthread_mutex_lock(&client->mutex);
    client->latest         = pos;
    client->last_packet_ms = monotonic_ms();
    pthread_mutex_unlock(&client->mutex);
}

/* --------------------------------------------------------------------------
 * RtkFeedFn implementation -- called once per tick on the UI thread
 * ----------------------------------------------------------------------- */

static void rtk_feed_client_fn(void *user, RtkFeedPosition *out)
{
    RtkFeedClient *client = (RtkFeedClient *)user;

    pthread_mutex_lock(&client->mutex);
    RtkFeedPosition snap     = client->latest;
    uint32_t        last_ms  = client->last_packet_ms;
    pthread_mutex_unlock(&client->mutex);

    uint32_t now = monotonic_ms();
    int stale = (last_ms == 0) || ((now - last_ms) > RTK_FEED_CLIENT_STALE_MS);

    if (stale) {
        memset(out, 0, sizeof(*out));
        out->valid = false;
        return;
    }

    *out = snap;
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

gm_status_t rtk_feed_client_start(RtkFeedClient *client, const char *host)
{
    memset(client, 0, sizeof(*client));
    pthread_mutex_init(&client->mutex, NULL);

    gm_status_t cs = stream_client_start(host, packet_callback, client);
    if (cs != GM_OK) {
        log_error("rtk_feed_client: stream_client_start failed");
        pthread_mutex_destroy(&client->mutex);
        return cs;
    }

    client->started = 1;
    log_info("rtk_feed_client: started, connecting to %s:%d", host, ROVER_PACKET_PORT);
    return GM_OK;
}

void rtk_feed_client_stop(RtkFeedClient *client)
{
    if (!client->started)
        return;

    stream_client_stop();
    pthread_mutex_destroy(&client->mutex);
    client->started = 0;
    log_info("rtk_feed_client: stopped");
}

RtkFeed rtk_feed_client_as_feed(RtkFeedClient *client)
{
    RtkFeed f = { .fn = rtk_feed_client_fn, .user = client };
    return f;
}