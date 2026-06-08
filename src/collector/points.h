/**
 * @file points.h
 * @brief Survey point storage with attributes.
 */

#ifndef GEOMARK_POINTS_H
#define GEOMARK_POINTS_H

#include <stdint.h>

#include "geomark.h"

#define GM_POINT_NAME_MAX 64
#define GM_POINTS_MAX 4096

typedef struct {
    uint32_t id;
    char name[GM_POINT_NAME_MAX];
    gm_position_t position;
    uint32_t timestamp_ms;
} gm_point_t;

typedef struct {
    gm_point_t points[GM_POINTS_MAX];
    uint32_t count;
} gm_point_store_t;

void points_init(gm_point_store_t *store);
gm_status_t points_add(gm_point_store_t *store, const gm_point_t *point);
gm_status_t points_get(const gm_point_store_t *store, uint32_t id, gm_point_t *out);

#endif /* GEOMARK_POINTS_H */