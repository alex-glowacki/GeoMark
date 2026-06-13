/**
 * @file net/stream_client.h
 * @brief TCP client that receives RoverStatusPacket from the pole-top unit.
 *
 * Usage:
 *   1. stream_client_start(host, callback, user) — spawns receive thread
 *   2. Callback is invoked on every successfully received packet
 *   3. stream_client_stop() — at shutdown
 *
 * The client auto-reconnects every 2 seconds if the connection drops.
 */

#ifndef GEOMARK_STREAM_CLIENT_H
#define GEOMARK_STREAM_CLIENT_H

#include "geomark.h"
#include "net/rover_packet.h"

/**
 * @brief Called by the receive thread for each complete, valid packet.
 *
 * @param pkt   Pointer to the received packet (valid only for duration of call).
 * @param user  User-supplied context pointer.
 */
typedef void (*StreamClientCallback)(const RoverStatusPacket *pkt, void *user);

/**
 * @brief Start the receive thread.
 *
 * @param host      Hostname or IP of the pole-top unit (e.g. "10.0.0.1").
 * @param callback  Called for each valid packet received.
 * @param user      Passed through to callback.
 * @return GM_OK on success, GM_ERR_IO if the thread could not be started.
 */
gm_status_t stream_client_start(const char *host, StreamClientCallback callback, void *user);

/**
 * @brief Stop the receive thread and close the socket.
 */
void stream_client_stop(void);

#endif /* GEOMARK_STREAM_CLIENT_H */