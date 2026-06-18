/**
 * @file ui/core/screen_stack.h
 * @brief Fixed-depth screen stack driving the new multi-screen nav tree
 *        (sleep → main menu → new project / continue / stats → job setup
 *        → measure points, per geomark-ui-redesign-decisions.md §1).
 *
 * Each screen is a small vtable (on_enter/on_event/on_tick/on_render/
 * on_exit) plus an opaque ctx pointer to its own state struct — the same
 * pattern SurveyScreenCtx already uses for one screen, generalized to a
 * stack of them so "Continue Project" can return to a screen that's still
 * alive underneath, not just re-created from scratch.
 *
 * on_enter is called both the first time a screen is pushed AND every time
 * it becomes the top of the stack again after a pop (i.e. it doubles as
 * "resume") — there is no separate on_resume callback, kept deliberately
 * simple for now.
 */

#ifndef GEOMARK_UI_CORE_SCREEN_STACK_H
#define GEOMARK_UI_CORE_SCREEN_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/core/ui_event.h"

#define UI_STACK_MAX_DEPTH 8

typedef struct {
    void (*on_enter)(void *ctx);
    bool (*on_event)(void *ctx, UiEvent ev); /* return true if consumed */
    void (*on_tick)(void *ctx, uint32_t now_ms);
    void (*on_render)(void *ctx);
    void (*on_exit)(void *ctx);
    void *ctx; /* opaque per-screen state; now owned by the stack */
} UiScreen;

typedef struct {
    UiScreen entries[UI_STACK_MAX_DEPTH];
    uint32_t depth;
} UiScreenStack;

void ui_stack_init(UiScreenStack *stack);

/** Push a new screen on top. Calls its on_enter. False if the stack is full. */
bool ui_stack_push(UiScreenStack *stack, UiScreen screen);

/** Pop the top screen (calls its on_exit), then resumes the one below (on_enter). */
bool ui_stack_pop(UiScreenStack *stack);

/** Replace the top screen in place: exit current top, push the new one. */
bool ui_stack_replace(UiScreenStack *stack, UiScreen screen);

UiScreen *ui_stack_top(UiScreenStack *stack);
bool ui_stack_is_empty(const UiScreenStack *stack);

/**
 * Dispatch one event to the top screen. If the top screen does not consume
 * a UI_EVENT_BACK itself (e.g. to show a discard-changes prompt), the stack
 * pops one level by default — this is what realizes "back to menu" for
 * every screen without each one re-implementing it. The root screen (depth
 * == 1) is never popped this way.
 */
void ui_stack_dispatch_event(UiScreenStack *stack, UiEvent ev);

void ui_stack_tick(UiScreenStack *stack, uint32_t now_ms);
void ui_stack_render(UiScreenStack *stack);

#endif /* GEOMARK_UI_CORE_SCREEN_STACK_H */