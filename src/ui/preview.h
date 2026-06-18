/**
 * @file ui/preview.h
 * @brief Manual, opt-in entry point for previewing the new screen-stack UI
 *        (Sleep -> Main Menu -> placeholder screens) on real display
 *        hardware, without touching ui/client.c's production flow.
 *
 * Launch manually over SSH — geomark-ui.service does NOT use this path:
 *   geomark --mode ui --ui-preview
 *
 * Controls (legacy GPIO d-pad, until the DSI touch panel replaces it):
 *   Up/Down  — move focus
 *   Center   — activate the focused widget
 *   Left     — back (matches the existing project-wide "Left = back"
 *              convention; no current screen needs Left for horizontal
 *              navigation yet — see preview.c's translate_input())
 *   Right    — reserved, currently a no-op
 *   Ctrl+C   — exit
 */

#ifndef GEOMARK_UI_PREVIEW_H
#define GEOMARK_UI_PREVIEW_H

#include "geomark.h"

gm_status_t ui_preview_run(void);

#endif /* GEOMARK_UI_PREVIEW_H */