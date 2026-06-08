/**
 * @file export.h
 * @brief CSV and DXF export for collected survey points.
 */

#ifndef GEOMARK_EXPORT_H
#define GEOMARK_EXPORT_H

#include "collector/points.h"

gm_status_t export_csv(const gm_point_store_t *store, const char *path);
gm_status_t export_dxf(const gm_point_store_t *store, const char *path);

#endif /* GEOMARK_EXPORT_H */