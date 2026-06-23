/**
 * @file ui/screens/placeholder_screen.h
 * @brief Generic "not built yet" screen — stands in for New Project, Job
 *        Setup, Measure Points, etc. until each is implemented for real.
 *
 * Has a grid with exactly one widget -- the standard back button (see
 * ui/core/widget.h's ui_grid_add_back_button() doc comment) -- so this
 * screen is still reachable with touch-only input, the same as every
 * other screen, now that the physical Left/BACK button is gone. Every
 * other event still falls through unconsumed so the screen stack's
 * default pop policy returns to whatever pushed it.
 */

#ifndef GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H
#define GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H

#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack; /* not owned */
    const char *message;  /* not owned; e.g. "New Project -- not built yet" */
} PlaceholderScreenCtx;

void placeholder_screen_init(PlaceholderScreenCtx *ctx, UiScreenStack *stack, const char *message);

/** Render implementation — placeholder_screen_draw.c (depends on ui/tft/display.h). */
void placeholder_screen_render(void *ctx);

UiScreen placeholder_screen_as_ui_screen(PlaceholderScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_PLACEHOLDER_SCREEN_H */