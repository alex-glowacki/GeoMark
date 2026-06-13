/**
 * @file main.c
 * @brief GeoMark entry point. Parses --mode flag and dispatches to
 *        base, rover, or UI station logic.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "geomark.h"
#include "base/station.h"
#include "rover/station.h"
#include "ui/client.h"
#include "util/log.h"

/* Default pole-top IP on the private geomark-rover WiFi network */
#define DEFAULT_POLE_TOP_HOST  "10.0.0.1"

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s --mode <base|rover|ui> [--config <path>] [--host <ip>]\n"
            "\n"
            "  --mode base    Base station: read UM980, relay RTCM3 via radio\n"
            "  --mode rover   Rover pole-top: receive corrections, stream fixes via WiFi\n"
            "  --mode ui      Handheld (Pi 5): receive fixes from pole-top, drive TFT\n"
            "\n"
            "  --config <path>  Config file (default: /etc/geomark/geomark.conf)\n"
            "  --host <ip>      Pole-top IP for UI mode (default: " DEFAULT_POLE_TOP_HOST ")\n",
            prog);
}

int main(int argc, char *argv[]) {
    geomark_mode_t mode        = GEOMARK_MODE_UNKNOWN;
    const char    *config_path = "/etc/geomark/geomark.conf";
    const char    *pole_top_host = DEFAULT_POLE_TOP_HOST;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "base") == 0) {
                mode = GEOMARK_MODE_BASE;
            } else if (strcmp(argv[i], "rover") == 0) {
                mode = GEOMARK_MODE_ROVER;
            } else if (strcmp(argv[i], "ui") == 0) {
                mode = GEOMARK_MODE_UI;
            } else {
                fprintf(stderr, "Unknown mode: %s\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            pole_top_host = argv[++i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (mode == GEOMARK_MODE_UNKNOWN) {
        fprintf(stderr, "Error: --mode is required.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    log_init(NULL); /* stderr logging until config is loaded */

    const char *mode_str = (mode == GEOMARK_MODE_BASE)  ? "base"  :
                           (mode == GEOMARK_MODE_ROVER) ? "rover" : "ui";
    log_info("GeoMark %s starting in %s mode", GEOMARK_VERSION_STRING, mode_str);

    int ret;
    if (mode == GEOMARK_MODE_BASE) {
        ret = base_station_run(config_path);
    } else if (mode == GEOMARK_MODE_ROVER) {
        ret = rover_station_run(config_path);
    } else {
        ret = ui_client_run(pole_top_host);
    }

    log_info("GeoMark exiting with status %d", ret);
    return (ret == GM_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}