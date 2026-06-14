/**
 * @file codelist.c
 * @brief Point code list loader implementation.
 */

#include "survey/survey.h"
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "survey/codelist.h"
#include "util/log.h"

/* --------------------------------------------------------------------------
 * Built-in defaults — used when no point_codes.txt is found
 * -------------------------------------------------------------------------- */

static const CodeEntry s_defaults[] = {
    { "CONC",  "Concrete"           },
    { "ASP",   "Asphalt"            },
    { "GRVL",  "Gravel"             },
    { "CURB",  "Curb"               },
    { "EP",    "Edge of pavement"   },
    { "BM",    "Benchmark"          },
    { "MON",   "Monument"           },
    { "TREE",  "Tree"               },
    { "FENCE", "Fence line"         },
    { "BLDG",  "Building corner"    },
    { "INV",   "Pipe invert"        },
    { "RIM",   "Manhole rim"        },
};

#define N_DEFAULTS (sizeof(s_defaults) / sizeof(s_defaults[0]))

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/** Strip leading and trailing whitespace in-place. */
static void trim(char *s) {
    /* leading */
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);

    /* trailing */
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

/**
 * Parse one line into @p entry.
 * Returns true if the line produced a valid entry.
 */
static bool parse_line(const char *line, CodeEntry *entry) {
    /* skip comments and blank lines */
    if (line[0] == '#' || line[0] == '\0')
        return false;

    memset(entry, 0, sizeof(*entry));

    const char *comma = strchr(line, ',');
    if (comma) {
        size_t code_len = (size_t)(comma - line);
        if (code_len == 0 || code_len >= SURVEY_CODE_MAX)
            return false;

        strncpy(entry->code, line, code_len);
        entry->code[code_len] = '\0';
        trim(entry->code);

        strncpy(entry->desc, comma + 1, SURVEY_DESC_MAX - 1);
        entry->desc[SURVEY_DESC_MAX - 1] = '\0';
        trim(entry->desc);
    } else {
        /* no comma — treat whole line as code, empty description */
        snprintf(entry->code, SURVEY_CODE_MAX, "%s", line);
        trim(entry->code);

        if (entry->code[0] == '\0')
            return false;
    }

    /* upper-case the code for consistency */
    for (char *c = entry->code; *c; c++)
        *c = (char)toupper((unsigned char)*c);

    return true;
}

/**
 * Try to load from @p path.
 * Returns true and populates @p list on success.
 */
static bool try_load(CodeList *list, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[SURVEY_CODE_MAX + SURVEY_DESC_MAX + 4];
    CodeEntry entry;
    uint32_t count = 0;

    while (fgets(line, sizeof(line), f) && count < CODELIST_MAX_ENTRIES) {
        /* strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (parse_line(line, &entry))
            list->entries[count++] = entry;
    }

    fclose(f);

    if (count == 0) {
        log_warn("codelist: %s is empty — falling back to defaults", path);
        return false;
    }

    list->count = count;
    list->from_file = true;
    strncpy(list->source, path, sizeof(list->source) - 1);
    log_info("codelist: loaded %u entries from %s", count, path);
    return true;
}

/** Load the built-in defaults into @p list. */
static void load_defaults(CodeList *list) {
    uint32_t n = (uint32_t)N_DEFAULTS;
    if (n > CODELIST_MAX_ENTRIES)
        n = CODELIST_MAX_ENTRIES;

    for (uint32_t i = 0; i < n; i++)
        list->entries[i] = s_defaults[i];

    list->count = n;
    list->from_file = false;
    strncpy(list->source, "<built-in defaults>", sizeof(list->source) - 1);
    log_info("codelist: using %u built-in defaults", n);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void codelist_load(CodeList *list) {
    memset(list, 0, sizeof(*list));

    static const char *search_paths[] = {
        CODELIST_USB_PATH,
        CODELIST_SD_PATH,
        CODELIST_SHARE_PATH,
    };
    static const uint32_t n_paths = 
        sizeof(search_paths) / sizeof(search_paths[0]);
    
    for (uint32_t i = 0; i < n_paths; i++) {
        if (try_load(list, search_paths[i]))
            return;
    }

    load_defaults(list);
}

void codelist_reload(CodeList *list) {
    codelist_load(list);
}

const CodeEntry *codelist_get(const CodeList *list, uint32_t index) {
    if (index >= list->count)
        return NULL;
    return &list->entries[index];
}

const CodeEntry *codelist_find(const CodeList *list, const char *code) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcasecmp(list->entries[i].code, code) == 0)
            return &list->entries[i];
    }
    return NULL;
}