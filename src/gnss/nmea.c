/**
 * @file nmea.c
 * @brief NMEA 0183 sentence parser implementation.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "gnss/nmea.h"
#include "util/log.h"

bool nmea_checksum_valid(const char *sentence)
{
    /* TODO: Phase 1 implementation */
    (void)sentence;
    return false;
}

gm_status_t nmea_parse_gga(const char *sentence, gm_nmea_gga_t *out)
{
    /* TODO: Phase 1 implementation */
    (void)sentence;
    (void)out;
    return GM_ERR_PARSE;
}

gm_status_t nmea_parse_rmc(const char *sentence, gm_nmea_rmc_t *out)
{
    /* TODO: Phase 1 implementation */
    (void)sentence;
    (void)out;
    return GM_ERR_PARSE;
}