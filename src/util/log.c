/**
 * @file log.c
 * @brief Structured logging implementation.
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "util/log.h"

static gm_log_level_t g_level = LOG_LEVEL_DEBUG;
static FILE          *g_fp    = NULL;

void log_init(const char *log_file_path)
{
    g_fp = stderr;
    if (log_file_path) {
        FILE *f = fopen(log_file_path, "a");
        if (f) {
            g_fp = f;
        }
    }
}

void log_set_level(gm_log_level_t level)
{
    g_level = level;
}

static void log_write(gm_log_level_t level, const char *prefix,
                      const char *fmt, va_list args)
{
    if (level < g_level) return;
    if (!g_fp) g_fp = stderr;

    time_t     now = time(NULL);
    struct tm *t   = localtime(&now);

    fprintf(g_fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec,
            prefix);
    vfprintf(g_fp, fmt, args);
    fprintf(g_fp, "\n");
    fflush(g_fp);
}

void log_debug(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log_write(LOG_LEVEL_DEBUG, "DEBUG", fmt, a);
    va_end(a);
}

void log_info(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log_write(LOG_LEVEL_INFO, "INFO", fmt, a);
    va_end(a);
}

void log_warn(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log_write(LOG_LEVEL_WARN, "WARN", fmt, a);
    va_end(a);
}

void log_error(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log_write(LOG_LEVEL_ERROR, "ERROR", fmt, a);
    va_end(a);
}