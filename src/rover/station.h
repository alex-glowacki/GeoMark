/**
 * @file rover/station.h
 * @brief Rover logic: receive corrections, output RTK positions.
 */

#ifndef GEOMARK_ROVER_STATION_H
#define GEOMARK_ROVER_STATION_H

#include "geomark.h"

gm_status_t rover_station_run(const char *config_path);

#endif /* GEOMARK_ROVER_STATION_H */