/**
 * @file ui/client.h
 * @brief UI client mode: receives packets from the pole-top, drives TFT.
 *
 * Runs on the Pi 5 handheld.  Connects to the Zero 2 W stream server,
 * receives RoverStatusPacket at 2 Hz, and renders the status screen.
 */

#ifndef GEOMARK_UI_CLIENT_H
#define GEOMARK_UI_CLIENT_H

#include "geomark.h"

/**
 * @brief Open the TFT, connect to the pole-top stream, and run the
 *        render loop until SIGINT or SIGTERM.
 *
 * @param pole_top_host  IP address or hostname of the Zero 2 W
 *                       (e.g. "10.0.0.1" in the field, "127.0.0.1" for testing).
 * @return GM_OK on clean shutdown, GM_ERR_IO if TFT init fails.
 */
gm_status_t ui_client_run(const char *pole_top_host);

#endif /* GEOMARK_UI_CLIENT_H */