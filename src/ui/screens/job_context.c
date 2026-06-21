/**
 * @file ui/screens/job_context.c
 * @brief See job_context.h.
 */

#define _GNU_SOURCE

#include "ui/screens/job_context.h"

#include <stdio.h>
#include <string.h>

void job_context_init(JobContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void job_context_set(JobContext *ctx, const char *home, const char *project_name,
                     const char *job_name)
{
    strncpy(ctx->name, job_name, sizeof(ctx->name) - 1);
    ctx->name[sizeof(ctx->name) - 1] = '\0';

    snprintf(ctx->job_dir, sizeof(ctx->job_dir), "%s/geomark-data/projects/%s/%s",
             home, project_name, job_name);
}

bool job_context_has_job(const JobContext *ctx)
{
    return ctx->name[0] != '\0';
}