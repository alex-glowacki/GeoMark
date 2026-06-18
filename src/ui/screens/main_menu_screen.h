/**
 * @file ui/screens/main_menu_screen.h
 * @brief Main menu — Start New Project / Continue Existing Project /
 *        View GeoMark Stats, per geomark-ui-redesign-decisions.md §2.
 */

#ifndef GEOMARK_UI_SCREENS_MAIN_MENU_SCREEN_H
#define GEOMARK_UI_SCREENS_MAIN_MENU_SCREEN_H

#include "ui/core/screen_stack.h"
#include "ui/core/widget.h"

typedef struct {
    UiWidgetGrid grid;
    UiScreenStack *stack;             /* not owned */
    UiScreen new_project_screen;      /* pushed by "Start New Project" */
    UiScreen continue_project_screen; /* pushed by "Continue Existing Project" */
    UiScreen stats_screen;            /* pushed by "View GeoMark Stats" */
} MainMenuScreenCtx;

/**
 * The three destination screens are caller-supplied so this screen never
 * needs to know what New Project / Job Setup / Measure Points actually
 * are — today they're simple stubs; swap in the real screens as each one
 * is built, with no change to this file.
 */
void main_menu_screen_init(MainMenuScreenCtx *ctx, UiScreenStack *stack,
                           UiScreen new_project_screen, UiScreen continue_project_screen,
                           UiScreen stats_screen);

/** Render implementation — main_menu_screen_draw.c (depends on ui/tft/display.h). */
void main_menu_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen main_menu_screen_as_ui_screen(MainMenuScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_MAIN_MENU_SCREEN_H */