/**
 * @file log.h
 * @brief Structured logging to stderr and optional log file.
 */

#ifndef GEOMARK_LOG_H
#define GEOMARK_LOG_H

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} gm_log_level_t;

void log_init(const char *log_file_path); /* NULL = stderr only */
void log_set_level(gm_log_level_t level);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif /* GEOMARK_LOG_H */