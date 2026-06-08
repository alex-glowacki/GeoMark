/**
 * @file main.c
 * @brief GeoMark entry point. Parses --mode flag and dispatches to
 *        base or rover station logic.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "geomark.h"
#include "base/station.h"
#include "rover/station.h"
#include "util/log.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s --mode <base|rover> [--config <path>]\n", prog);
}

int main(int argc, char *argv[]) {
    geomark_mode_t mode = GEOMARK_MODE_UNKNOWN;
    const char *config_path = "/etc/geomark/geomark.conf";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "base") == 0) {
                mode = GEOMARK_MODE_BASE;
            } else if (strcmp(argv[i], "rover") == 0) {
                mode = GEOMARK_MODE_ROVER;
            } else {
                fprintf(stderr, "Unknown mode: %s\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
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

    log_info("GeoMark %s starting in %s mode",
            GEOMARK_VERSION_STRING,
            mode == GEOMARK_MODE_BASE ? "base" : "rover");

    int ret;
    if (mode == GEOMARK_MODE_BASE) {
        ret = base_station_run(config_path);
    } else {
        ret = rover_station_run(config_path);
    }

    log_info("GeoMark exiting with status %d", ret);
    return ret = GM_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}