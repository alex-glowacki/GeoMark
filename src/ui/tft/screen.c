/**
 * @file screen.c
 * @brief RTK status screen layout for the rover TFT display.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ui/tft/screen.h"
#include "ui/tft/display.h"
#include "util/units.h"

/* -------------------------------------------------------------------------
 * Layout constants — all coordinates in pixels
 * ---------------------------------------------------------------------- */

/* Background and chrome */
#define COL_BG          TFT_BLACK
#define COL_CHROME      TFT_DKGRAY
#define COL_LABEL       TFT_GRAY
#define COL_VALUE       TFT_WHITE
#define COL_TITLE       TFT_CYAN

/* Fix badge colors */
#define COL_FIX_NONE    TFT_RED
#define COL_FIX_SINGLE  TFT_ORANGE
#define COL_FIX_DGPS    TFT_YELLOW
#define COL_FIX_FLOAT   TFT_CYAN
#define COL_FIX_FIXED   TFT_GREEN

/* Text scales */
#define SCALE_TITLE  2   /* 10x14 px per glyph */
#define SCALE_LABEL  2
#define SCALE_VALUE  3   /* 15x21 px per glyph — large data values */
#define SCALE_SMALL  2

/* Horizontal divider Y positions */
#define DIV_Y1  44   /* below title bar */
#define DIV_Y2  220  /* above status bar */

/* Title bar */
#define TITLE_Y   12
#define TITLE_X  160

/* Fix badge */
#define BADGE_X    8
#define BADGE_Y    6
#define BADGE_W  140
#define BADGE_H   32

/* Data rows — Y positions of top of value text */
#define ROW_LAT_Y   60
#define ROW_LON_Y  110
#define ROW_ALT_Y  160

/* Label X */
#define LABEL_X   8
/* Value X (right of labels) */
#define VALUE_X  90

/* Status bar row */
#define STATUS_Y  235

/* Status bar column X positions */
#define HDOP_LABEL_X    8
#define HDOP_VALUE_X   90
#define SATS_LABEL_X  180
#define SATS_VALUE_X  240
#define AGE_LABEL_X   340
#define AGE_VALUE_X   390

/* Width of a value field background erase rectangle */
#define VALUE_ERASE_W  300
#define VALUE_ERASE_H   28   /* SCALE_VALUE * TFT_FONT_H + 1 px */
#define SMALL_ERASE_W   80
#define SMALL_ERASE_H   18   /* SCALE_SMALL * TFT_FONT_H + 1 px */

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/* Draw a label string (static — called once from screen_init). */
static void draw_label(uint16_t x, uint16_t y, const char *s)
{
    display_draw_string(x, y, s, COL_LABEL, COL_BG, SCALE_LABEL);
}

/*
 * Erase a value field and redraw with new text.
 * w and h are the erase rectangle dimensions.
 */
static void update_value(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         const char *s, uint16_t col, uint8_t scale)
{
    display_fill_rect(x, y, w, h, COL_BG);
    display_draw_string(x, y, s, col, COL_BG, scale);
}

/* Return the RGB565 color for a fix badge background. */
static uint16_t badge_color(gm_fix_type_t ft)
{
    switch (ft) {
        case FIX_RTK_FIXED: return COL_FIX_FIXED;
        case FIX_RTK_FLOAT: return COL_FIX_FLOAT;
        case FIX_DGPS:      return COL_FIX_DGPS;
        case FIX_SINGLE:    return COL_FIX_SINGLE;
        default:            return COL_FIX_NONE;
    }
}

/* Return the short label string for a fix type. */
static const char *badge_label(gm_fix_type_t ft)
{
    switch (ft) {
        case FIX_RTK_FIXED: return "RTK FIXED";
        case FIX_RTK_FLOAT: return "RTK FLOAT";
        case FIX_DGPS:      return "DGPS     ";
        case FIX_SINGLE:    return "SINGLE   ";
        default:            return "NO FIX   ";
    }
}

/* -------------------------------------------------------------------------
 * Static chrome — drawn once
 * ---------------------------------------------------------------------- */

void screen_init(void)
{
    display_fill(COL_BG);

    /* Horizontal dividers */
    display_fill_rect(0, DIV_Y1, TFT_WIDTH, 2, COL_CHROME);
    display_fill_rect(0, DIV_Y2, TFT_WIDTH, 2, COL_CHROME);

    /* Title */
    display_draw_string(TITLE_X, TITLE_Y, "GeoMark Rover",
                        COL_TITLE, COL_BG, SCALE_TITLE);

    /* Data labels */
    draw_label(LABEL_X, ROW_LAT_Y,  "LAT");
    draw_label(LABEL_X, ROW_LON_Y,  "LON");
    draw_label(LABEL_X, ROW_ALT_Y,  "ALT");

    /* Status bar labels */
    draw_label(HDOP_LABEL_X, STATUS_Y, "HDOP");
    draw_label(SATS_LABEL_X, STATUS_Y, "SATS");
    draw_label(AGE_LABEL_X,  STATUS_Y, "AGE");
}

/* -------------------------------------------------------------------------
 * Dynamic update
 * ---------------------------------------------------------------------- */

void screen_update(const gm_position_t *pos, bool valid, uint32_t now_ms)
{
    char buf[32];

    /* ------------------------------------------------------------------ */
    /* Fix badge                                                           */
    /* ------------------------------------------------------------------ */
    gm_fix_type_t ft = valid ? pos->fix_type : FIX_NONE;
    uint16_t bc = badge_color(ft);

    display_fill_rect(BADGE_X, BADGE_Y, BADGE_W, BADGE_H, bc);
    /* Badge text — black on colored background */
    display_draw_string(BADGE_X + 6, BADGE_Y + 8,
                        badge_label(ft),
                        TFT_BLACK, bc, SCALE_LABEL);

    if (!valid) {
        /* No fix yet — blank all value fields */
        update_value(VALUE_X, ROW_LAT_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                     "---", COL_VALUE, SCALE_VALUE);
        update_value(VALUE_X, ROW_LON_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                     "---", COL_VALUE, SCALE_VALUE);
        update_value(VALUE_X, ROW_ALT_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                     "---", COL_VALUE, SCALE_VALUE);
        update_value(HDOP_VALUE_X, STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                     "--.-", COL_VALUE, SCALE_SMALL);
        update_value(SATS_VALUE_X, STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                     "--", COL_VALUE, SCALE_SMALL);
        update_value(AGE_VALUE_X,  STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                     "--s", COL_VALUE, SCALE_SMALL);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Latitude                                                            */
    /* ------------------------------------------------------------------ */
    snprintf(buf, sizeof(buf), "%.8f%c",
             pos->latitude  >= 0.0 ? pos->latitude  : -pos->latitude,
             pos->latitude  >= 0.0 ? 'N' : 'S');
    update_value(VALUE_X, ROW_LAT_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                 buf, COL_VALUE, SCALE_VALUE);

    /* ------------------------------------------------------------------ */
    /* Longitude                                                           */
    /* ------------------------------------------------------------------ */
    snprintf(buf, sizeof(buf), "%.8f%c",
             pos->longitude >= 0.0 ? pos->longitude : -pos->longitude,
             pos->longitude >= 0.0 ? 'E' : 'W');
    update_value(VALUE_X, ROW_LON_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                 buf, COL_VALUE, SCALE_VALUE);

    /* ------------------------------------------------------------------ */
    /* Altitude — international feet for elevation display                */
    /* ------------------------------------------------------------------ */
    double alt_ft = gm_m_to_intl_ft(pos->altitude);
    snprintf(buf, sizeof(buf), "%.1f ft", alt_ft);
    update_value(VALUE_X, ROW_ALT_Y, VALUE_ERASE_W, VALUE_ERASE_H,
                 buf, COL_VALUE, SCALE_VALUE);

    /* ------------------------------------------------------------------ */
    /* HDOP                                                                */
    /* ------------------------------------------------------------------ */
    snprintf(buf, sizeof(buf), "%.1f", pos->hdop);
    update_value(HDOP_VALUE_X, STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                 buf, COL_VALUE, SCALE_SMALL);

    /* ------------------------------------------------------------------ */
    /* Satellites                                                          */
    /* ------------------------------------------------------------------ */
    snprintf(buf, sizeof(buf), "%02u", pos->satellites);
    update_value(SATS_VALUE_X, STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                 buf, COL_VALUE, SCALE_SMALL);

    /* ------------------------------------------------------------------ */
    /* Age of fix                                                          */
    /* ------------------------------------------------------------------ */
    uint32_t age_ms = now_ms - pos->timestamp_ms;
    uint32_t age_s  = age_ms / 1000u;
    if (age_s > 99) age_s = 99;
    snprintf(buf, sizeof(buf), "%us", age_s);
    update_value(AGE_VALUE_X, STATUS_Y, SMALL_ERASE_W, SMALL_ERASE_H,
                 buf,
                 age_s >= 5 ? TFT_ORANGE : COL_VALUE, /* warn if stale */
                 SCALE_SMALL);
}