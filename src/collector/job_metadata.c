/**
 * @file job_metadata.c
 * @brief Job metadata defaults, unit coercion, and INI-style save/load.
 *        See job_metadata.h for why this is a separate module rather
 *        than an extension of util/config.c's config_load().
 */

#define _GNU_SOURCE

#include "collector/job_metadata.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"

#define GM_JOB_INI_MAX_LINE 384

void job_metadata_defaults(gm_job_metadata_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->template_name, "Default", sizeof(out->template_name) - 1);
    out->coord_sys = GM_COORD_SYS_WGS84;
    out->dist_unit = GM_DIST_UNIT_US_SURVEY_FOOT;
    out->cogo      = GM_COGO_GROUND;
}

void job_metadata_coerce_units(gm_job_metadata_t *meta)
{
    if (meta->coord_sys == GM_COORD_SYS_ND_NORTH)
        meta->dist_unit = GM_DIST_UNIT_INTL_FOOT;
}

/* -------------------------------------------------------------------------
 * Enum <-> string. Stored on disk as names, not raw integers, so the file
 * stays human-readable/editable and is not silently broken by a future
 * reordering of the enum values.
 * ---------------------------------------------------------------------- */

static const char *coord_sys_to_str(gm_coord_sys_t v)
{
    switch (v) {
    case GM_COORD_SYS_WGS84:        return "WGS84";
    case GM_COORD_SYS_UTM:          return "UTM";
    case GM_COORD_SYS_LOCAL_GROUND: return "LOCAL_GROUND";
    case GM_COORD_SYS_ND_NORTH:     return "ND_NORTH";
    default:                        return "WGS84";
    }
}

static gm_coord_sys_t coord_sys_from_str(const char *s)
{
    if (strcmp(s, "UTM") == 0)          return GM_COORD_SYS_UTM;
    if (strcmp(s, "LOCAL_GROUND") == 0) return GM_COORD_SYS_LOCAL_GROUND;
    if (strcmp(s, "ND_NORTH") == 0)     return GM_COORD_SYS_ND_NORTH;
    return GM_COORD_SYS_WGS84;
}

static const char *dist_unit_to_str(gm_dist_unit_t v)
{
    switch (v) {
    case GM_DIST_UNIT_US_SURVEY_FOOT: return "US_SURVEY_FOOT";
    case GM_DIST_UNIT_INTL_FOOT:      return "INTL_FOOT";
    default:                         return "US_SURVEY_FOOT";
    }
}

static gm_dist_unit_t dist_unit_from_str(const char *s)
{
    if (strcmp(s, "INTL_FOOT") == 0) return GM_DIST_UNIT_INTL_FOOT;
    return GM_DIST_UNIT_US_SURVEY_FOOT;
}

static const char *cogo_to_str(gm_cogo_t v)
{
    return (v == GM_COGO_GRID) ? "GRID" : "GROUND";
}

static gm_cogo_t cogo_from_str(const char *s)
{
    return (strcmp(s, "GRID") == 0) ? GM_COGO_GRID : GM_COGO_GROUND;
}

/* -------------------------------------------------------------------------
 * Save
 * ---------------------------------------------------------------------- */

gm_status_t job_metadata_save(const char *path, const gm_job_metadata_t *meta)
{
    gm_job_metadata_t coerced = *meta;
    job_metadata_coerce_units(&coerced);

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("job_metadata_save: cannot open '%s': %s", path, strerror(errno));
        return GM_ERR_IO;
    }

    fprintf(f, "job_name=%s\n",     coerced.job_name);
    fprintf(f, "template=%s\n",     coerced.template_name);
    fprintf(f, "coord_sys=%s\n",    coord_sys_to_str(coerced.coord_sys));
    fprintf(f, "dist_unit=%s\n",    dist_unit_to_str(coerced.dist_unit));
    fprintf(f, "cogo=%s\n",         cogo_to_str(coerced.cogo));
    fprintf(f, "reference=%s\n",    coerced.reference);
    fprintf(f, "description=%s\n", coerced.description);
    fprintf(f, "operator=%s\n",     coerced.operator_name);
    fprintf(f, "notes=%s\n",        coerced.notes);

    fclose(f);
    log_info("job_metadata_save: wrote '%s'", path);
    return GM_OK;
}

/* -------------------------------------------------------------------------
 * Load -- same line-scanning shape as util/config.c's config_load():
 * strip comments/blanks, split on first '=', match known keys, warn and
 * skip anything else.
 * ---------------------------------------------------------------------- */

gm_status_t job_metadata_load(const char *path, gm_job_metadata_t *out)
{
    job_metadata_defaults(out);

    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("job_metadata_load: cannot open '%s': %s -- using defaults",
                 path, strerror(errno));
        return GM_OK;
    }

    char line[GM_JOB_INI_MAX_LINE];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == '#' || line[0] == ';')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            log_warn("job_metadata_load: %s:%d: malformed line (no '=') -- skipped",
                     path, lineno);
            continue;
        }

        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "job_name") == 0) {
            strncpy(out->job_name, val, sizeof(out->job_name) - 1);
        } else if (strcmp(key, "template") == 0) {
            strncpy(out->template_name, val, sizeof(out->template_name) - 1);
        } else if (strcmp(key, "coord_sys") == 0) {
            out->coord_sys = coord_sys_from_str(val);
        } else if (strcmp(key, "dist_unit") == 0) {
            out->dist_unit = dist_unit_from_str(val);
        } else if (strcmp(key, "cogo") == 0) {
            out->cogo = cogo_from_str(val);
        } else if (strcmp(key, "reference") == 0) {
            strncpy(out->reference, val, sizeof(out->reference) - 1);
        } else if (strcmp(key, "description") == 0) {
            strncpy(out->description, val, sizeof(out->description) - 1);
        } else if (strcmp(key, "operator") == 0) {
            strncpy(out->operator_name, val, sizeof(out->operator_name) - 1);
        } else if (strcmp(key, "notes") == 0) {
            strncpy(out->notes, val, sizeof(out->notes) - 1);
        } else {
            log_warn("job_metadata_load: %s:%d: unknown key '%s' -- ignored",
                     path, lineno, key);
        }
    }

    fclose(f);
    job_metadata_coerce_units(out);
    log_info("job_metadata_load: loaded '%s'", path);
    return GM_OK;
}