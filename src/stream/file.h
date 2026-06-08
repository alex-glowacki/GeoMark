/**
 * @file file.h
 * @brief File stream for data logging and export.
 */

#ifndef GEOMARK_FILE_H
#define GEOMARK_FILE_H

#include <stdio.h>

#include "geomark.h"

typedef struct {
    FILE *fp;
    char path[256];
} gm_file_t;

gm_status_t file_open(gm_file_t *f, const char *path, const char *mode);
gm_status_t file_write(gm_file_t *f, const char *buf, size_t len);
void file_close(gm_file_t *f);

#endif /* GEOMARK_FILE_H */