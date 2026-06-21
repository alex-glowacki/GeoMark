/**
 * @file ui/screens/project_context.c
 * @brief See project_context.h.
 */

#define _GNU_SOURCE

#include "ui/screens/project_context.h"

#include <string.h>

void project_context_init(ProjectContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void project_context_set(ProjectContext *ctx, const char *name)
{
    strncpy(ctx->name, name, sizeof(ctx->name) - 1);
    ctx->name[sizeof(ctx->name) - 1] = '\0';
}

bool project_context_has_project(const ProjectContext *ctx)
{
    return ctx->name[0] != '\0';
}