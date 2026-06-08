/**
 * @file file.c
 * @brief File stream implementation.
 */

#include "stream/file.h"
#include "util/log.h"

gm_status_t file_open(gm_file_t *f, const char *path, const char *mode)
{
    /* TODO: Phase 3 implementation */
    (void)f;
    (void)path;
    (void)mode;
    log_warn("file_open: not yet implemented");
    return GM_ERR_GENERIC;
}

gm_status_t file_write(gm_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return GM_ERR_GENERIC;
}

void file_close(gm_file_t *f)
{
    if (f && f->fp) {
        fclose(f->fp);
        f->fp = NULL;
    }
}