/**
 * @file net/stream_server.h
 * @brief TCP server that streams RoverStatusPacket at 2 Hz to the handheld.
 *
 * Usage:
 *   1. stream_server_start()   — call once at startup (spawns accept thread)
 *   2. stream_server_broadcast() — call from the 2 Hz loop with latest packet
 *   3. stream_server_stop()    — call at shutdown
 *
 * Only one client is supported at a time.  If a second client connects
 * while one is already connected, the old connection is closed first.
 */

#ifndef GEOMARK_STREAM_SERVER_H
#define GEOMARK_STREAM_SERVER_H

#include "geomark.h"
#include "net/rover_packet.h"

/**
 * @brief Start the TCP listen socket and accept thread.
 *
 * Binds to 0.0.0.0:ROVER_PACKET_PORT so it works for both the
 * production deployment (10.0.0.1) and host-debug testing (127.0.0.1).
 *
 * @return GM_OK on success, GM_ERR_IO on failure.
 */
gm_status_t stream_server_start(void);

/**
 * @brief Broadcast a status packet to the connected client (if any).
 *
 * If no client is connected, the call is a no-op.
 * Thread-safe: may be called from the main station loop.
 *
 * @param pkt  Pointer to the packet to send.
 */
void stream_server_broadcast(const RoverStatusPacket *pkt);

/**
 * @brief Stop the accept thread, close all sockets, and free resources.
 */
void stream_server_stop(void);

#endif /* GEOMARK_STREAM_SERVER_H */