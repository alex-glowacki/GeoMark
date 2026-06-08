/**
 * @file points.c
 * @brief Survey point storage implementation.
 */

#include <string.h>

#include "collector/points.h"

void points_init(gm_point_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

gm_status_t points_add(gm_point_store_t *store, const gm_point_t *point)
{
    /* TODO: Phase 3 implementation */
    (void)store;
    (void)point;
    return GM_ERR_GENERIC;
}

gm_status_t points_get(const gm_point_store_t *store, uint32_t id, gm_point_t *out)
{
    /* TODO: Phase 3 implementation */
    (void)store;
    (void)id;
    (void)out;
    return GM_ERR_GENERIC;
}