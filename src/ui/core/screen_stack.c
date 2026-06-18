/**
 * @file ui/core/screen_stack.c
 * @brief Screen stack logic. No ui/tft/display.h dependency — a screen's
 *        own on_render may call into display.h, but the stack mechanics
 *        (push/pop/replace/dispatch) don't need to.
 */

#define _GNU_SOURCE

#include "ui/core/screen_stack.h"

#include <string.h>

#include "util/log.h"

void ui_stack_init(UiScreenStack *stack)
{
    memset(stack, 0, sizeof(*stack));
}

bool ui_stack_push(UiScreenStack *stack, UiScreen screen)
{
    if (stack->depth >= UI_STACK_MAX_DEPTH) {
        log_error("ui/screen_stack: push exceeds max depth %d", UI_STACK_MAX_DEPTH);
        return false;
    }
    stack->entries[stack->depth] = screen;
    stack->depth++;

    UiScreen *top = &stack->entries[stack->depth - 1];
    if (top->on_enter)
        top->on_enter(top->ctx);
    return true;
}

bool ui_stack_pop(UiScreenStack *stack)
{
    if (stack->depth == 0) {
        log_error("ui/screen_stack: pop on empty stack");
        return false;
    }

    UiScreen *cur = &stack->entries[stack->depth - 1];
    if (cur->on_exit)
        cur->on_exit(cur->ctx);
    stack->depth--;

    if (stack->depth > 0) {
        UiScreen *resumed = &stack->entries[stack->depth - 1];
        if (resumed->on_enter)
            resumed->on_enter(resumed->ctx);
    }
    return true;
}

bool ui_stack_replace(UiScreenStack *stack, UiScreen screen)
{
    if (stack->depth > 0) {
        UiScreen *cur = &stack->entries[stack->depth - 1];
        if (cur->on_exit)
            cur->on_exit(cur->ctx);
        stack->depth--;
    }
    return ui_stack_push(stack, screen);
}

UiScreen *ui_stack_top(UiScreenStack *stack)
{
    if (stack->depth == 0)
        return NULL;
    return &stack->entries[stack->depth - 1];
}

bool ui_stack_is_empty(const UiScreenStack *stack)
{
    return stack->depth == 0;
}

void ui_stack_dispatch_event(UiScreenStack *stack, UiEvent ev)
{
    UiScreen *top = ui_stack_top(stack);
    if (!top)
        return;

    bool consumed = top->on_event ? top->on_event(top->ctx, ev) : false;

    if (!consumed && ev.type == UI_EVENT_BACK && stack->depth > 1)
        ui_stack_pop(stack);
}

void ui_stack_tick(UiScreenStack *stack, uint32_t now_ms)
{
    UiScreen *top = ui_stack_top(stack);
    if (top && top->on_tick)
        top->on_tick(top->ctx, now_ms);
}

void ui_stack_render(UiScreenStack *stack)
{
    UiScreen *top = ui_stack_top(stack);
    if (top && top->on_render)
        top->on_render(top->ctx);
}