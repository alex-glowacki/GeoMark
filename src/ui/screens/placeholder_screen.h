/**
 * @file ui/screens/placeholder_screen.h
 * @brief Generic "not built yet" screen — stands in for New Project, Job
 *        Setup, Measure Points, etc. until each is implemented for real.
 *
 * No widgets, no focus. Every event, including BACK, falls through
 * unconsumed so the screen stack's default pop policy returns to
 * whatever pushed it.
 */

#ifndef GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H
#define GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H

#include "ui/core/screen_stack.h"

typedef struct {
    const char *message; /* not owned; e.g. "New Project -- not built yet" */
} PlaceholderScreenCtx;

void placeholder_screen_init(PlaceholderScreenCtx *ctx, const char *message);

/** Render implementation — placeholder_screen_draw.c (depends on ui/tft/display.h). */
void placeholder_screen_render(void *ctx);

UiScreen placeholder_screen_as_ui_screen(PlaceholderScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H */