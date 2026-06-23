/**
 * @file ui/screens/sleep_screen.h
 * @brief Sleep / inactive screen — root of the new screen stack.
 *
 * Per geomark-ui-redesign-decisions.md §2: custom image/text (content still
 * TBD — see §5 open questions) with "tap anywhere to wake". Touch-only
 * input (the physical GPIO d-pad is no longer read by this UI -- see
 * ui/preview.h's controls doc), so any tap on the screen wakes it.
 */

#ifndef GEOMARK_UI_SCREENS_SLEEP_SCREEN_H
#define GEOMARK_UI_SCREENS_SLEEP_SCREEN_H

#include "ui/core/screen_stack.h"

typedef struct {
    UiScreenStack *stack; /* not owned */
    UiScreen wake_target; /* screen pushed on any input — e.g. Main Menu */
} SleepScreenCtx;

void sleep_screen_init(SleepScreenCtx *ctx, UiScreenStack *stack, UiScreen wake_target);

/** Render implementation — sleep_screen_draw.c (depends on ui/tft/display.h). */
void sleep_screen_render(void *ctx);

/** Build the UiScreen vtable entry for this screen. */
UiScreen sleep_screen_as_ui_screen(SleepScreenCtx *ctx);

#endif /* GEOMARK_UI_SCREENS_SLEEP_SCREEN_H */