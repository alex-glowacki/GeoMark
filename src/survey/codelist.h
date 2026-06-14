/**
 * @file codelist.h
 * @brief Point code list loader.
 *
 * Loads a plain-text point code list from disk.  Format is one entry per
 * line:
 *
 *   CODE,Description
 *
 * Lines beginning with '#' are comments and are ignored.  The description
 * field is optional — a line with no comma is treated as a code with an
 * empty description.
 *
 * Search order:
 *   1. USB stick:  /media/usb/geomark/point_codes.txt
 *   2. SD card:    ~/geomark/point_codes.txt
 *   3. Repo data:  /usr/local/share/geomark/point_codes.txt
 *   4. Built-in hardcoded defaults (always available)
 */

#ifndef GEOMARK_CODELIST_H
#define GEOMARK_CODELIST_H

#include <stdbool.h>
#include <stdint.h>

#include "survey/survey.h"

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

#define CODELIST_MAX_ENTRIES 128

#define CODELIST_USB_PATH "/media/usb/geomark/point_codes.txt"
#define CODELIST_SD_PATH "/home/alex/geomark/point_codes.txt"
#define CODELIST_SHARE_PATH "/usr/local/share/geomark/point_codes.txt"

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

typedef struct {
    char code[SURVEY_CODE_MAX];
    char desc[SURVEY_DESC_MAX];
} CodeEntry;

typedef struct {
    CodeEntry entries[CODELIST_MAX_ENTRIES];
    uint32_t count;
    bool from_file;   /* false = built-in defaults were used */
    char source[256]; /* path that was actually loaded */
} CodeList;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * Load the point code list, trying each search path in order.
 * Always succeeds — falls back to built-in defaults if no file is found.
 *
 * @param list  Caller-allocated CodeList (zeroed by this call).
 */
void codelist_load(CodeList *list);

/**
 * Reload the list from disk.  Useful if the user swaps the USB stick.
 * Equivalent to codelist_load() on a pre-existing struct.
 */
void codelist_reload(CodeList *list);

/**
 * Return the entry at @p index, or NULL if out of range.
 */
const CodeEntry *codelist_get(const CodeList *list, uint32_t index);

/**
 * Find a code by exact string match (case-insensitive).
 * Returns the matching entry or NULL if not found.
 */
const CodeEntry *codelist_find(const CodeList *list, const char *code);

#endif /* GEOMARK_CODELIST_H */