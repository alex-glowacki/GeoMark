/**
 * @file base/station.h
 * @brief Base station logic: read UM980, relay RTCM3 corrections via radio.
 */

#ifndef GEOMARK_BASE_STATION_H
#define GEOMARK_BASE_STATION_H

#include "geomark.h"

gm_status_t base_station_run(const char *config_path);

#endif /* GEOMARK_BASE_STATION_H */