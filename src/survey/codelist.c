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
    /* --- Point codes (ACG_TOPO_V1.xlsx, KLJ_TOPO_V4_Beta, "Point codes" section) --- */
    { "AC",       "Air Conditioner" },
    { "ANODE",    "Anode" },
    { "ARV",      "Air Release Valve" },
    { "AZI",      "NGS Azimuth Mark" },
    { "BH",       "Bee Hive Inlet" },
    { "BHL",      "Bottom Hole Location" },
    { "BM",       "Benchmark" },
    { "BP",       "Brace Pole" },
    { "CBR",      "Catch Basin Round" },
    { "CBS",      "Catch Basin Square" },
    { "CCT",      "Card Control Machine" },
    { "CHK",      "Check Point" },
    { "CIO",      "Curb Inlet Only" },
    { "CORCALC",  "Calculated Corner" },
    { "CORCL",    "Closing Corner" },
    { "CORMEA",   "Meander Corner" },
    { "CORMP",    "Boundary Mile Post Corner" },
    { "CORPROP",  "Property Corner" },
    { "CORQTR",   "Quarter Corner" },
    { "CORREF",   "Reference Corner" },
    { "CORROW",   "Right of Way Corner" },
    { "CORSEC",   "Section Corner" },
    { "CORSET",   "Set Corner" },
    { "CORSIX",   "Sixteenth Corner" },
    { "CORTIE",   "Tie for Cadastral Monuments" },
    { "CORWIT",   "Witness Corner" },
    { "CP",       "Control Point" },
    { "CTOWP",    "Communications Tower (Point)" },
    { "DOORP",    "Door (Point)" },
    { "DSPT",     "Downspout" },
    { "ELM",      "Electric Meter" },
    { "EP",       "Electric Pedestal" },
    { "FFP",      "Fuel Filler Pipe" },
    { "FGP",      "Flag Pole" },
    { "FLR",      "Culvert Flair End (Measure at Center of Flair)" },
    { "FLS",      "Fuel Leak Sensor" },
    { "FOTPC",    "Telephone Fiber Optic Pedestal" },
    { "FOTVP",    "Fiber Pedestal" },
    { "FP",       "Fuel Dispenser" },
    { "FRG",      "Railroad Frog" },
    { "FVP",      "Fuel Vent Pipe" },
    { "GMTR",     "Gas Meter" },
    { "GMV",      "Gas Main Valve" },
    { "GP",       "Guard Post" },
    { "GREG",     "Gas Line Regulator" },
    { "GSV",      "Gas Service Valve" },
    { "GTX",      "Ground Truthing X-Sec" },
    { "GVP",      "Gas Vent Pipe" },
    { "GWM",      "Ground Water Monitor Well" },
    { "GYP",      "Guy Pole" },
    { "HB",       "Hose Bib" },
    { "HV",       "Aerial Target" },
    { "HVPP",     "Aerial Target Picture Point" },
    { "HYD",      "Fire Hydrant" },
    { "IM",       "Inlet MH" },
    { "INS",      "Insulator" },
    { "L1O",      "LiDAR Point (Open Terrain)" },
    { "L2T",      "LiDAR Point (Crops)" },
    { "L3B",      "LiDAR Point (Brush/Low Trees)" },
    { "L4F",      "LiDAR Point (Forested)" },
    { "L5U",      "LiDAR Point (Urban)" },
    { "LPC",      "Light Pole (Concrete)" },
    { "LPS",      "Light Pole (Steel)" },
    { "LPTS",     "Light Pole w/Traffic Signal" },
    { "LPW",      "Light Pole (Wood)" },
    { "MB",       "Mailbox" },
    { "MHAV",     "Manhole w/Air Release Valve" },
    { "MHE",      "Electric MH" },
    { "MHFS",     "Sanitary Manhole Force Main" },
    { "MHG",      "Gas Manhole" },
    { "MHS",      "Sanitary MH" },
    { "MHST",     "Storm MH" },
    { "MHT",      "Telephone MH" },
    { "MHU",      "Unidentified Manhole" },
    { "MHW",      "Water MH" },
    { "MHWM",     "Manhole Water w/Meter" },
    { "MHWV",     "Manhole Water w/Valve" },
    { "MISCP",    "Misc. Point" },
    { "OS",       "Offset (Measured Point)" },
    { "OW",       "Oil Well Stake" },
    { "PAD",      "Pad Corner" },
    { "PBX",      "Pull Box" },
    { "POL",      "Pole" },
    { "POST",     "Post" },
    { "PP",       "Power Pole" },
    { "PPL",      "Power Pole with Light" },
    { "PPS",      "Power Pole Structure" },
    { "PPTR",     "Power Pole w/Transformer" },
    { "PTS",      "Pay Tele Stand" },
    { "PTWP",     "Push to Walk Post/ Ped Xing Post" },
    { "REF",      "NGS Reference Mark" },
    { "RIME",     "Rim Elevation" },
    { "RM",       "Reference Marker (Mile Posts)" },
    { "RRBB",     "Railroad Battery Box" },
    { "RRCB",     "Railroad Cross Buck" },
    { "RRS",      "Railroad Signal" },
    { "RRSA",     "Railroad Signal w/Arm" },
    { "RRSPOT",   "Railroad Spot Shot" },
    { "RSTK",     "Restaked Point" },
    { "RWMRKC",   "ROW Marker Concrete" },
    { "RWMRKF",   "ROW Marker Fiberglass Post" },
    { "RWMRKP",   "ROW Marker Plastic Post" },
    { "RWMRKS",   "ROW Marker Steel" },
    { "RWMRKW",   "ROW Marker Wood" },
    { "SB",       "Shrub" },
    { "SBR",      "Soil Boring" },
    { "SGN",      "Traffic Sign (1 Post)" },
    { "SIGN",     "Private Sign (1 Post)" },
    { "SMH",      "Manhole Steam" },
    { "SNVP",     "Sanitary Vent Pipe" },
    { "SPIG",     "Spigot (Garden Hose Connection)" },
    { "SPK",      "Sprinkler Head" },
    { "SPKV",     "Sprinkler Valve" },
    { "SPOT",     "Spot Elevation" },
    { "SPR",      "Soil Probe" },
    { "SRCP",     "Slotted Drain in RCP Culvert" },
    { "SSCLN",    "Sanitary C/O" },
    { "SSGV",     "Sanitary System Gate Valve" },
    { "SSO",      "Storm Sewer Outlet" },
    { "STK",      "Staked Point" },
    { "STP",      "Stump" },
    { "TB",       "Telephone Box" },
    { "TCP",      "Temporary Control Point" },
    { "THRES",    "Threshold" },
    { "TL",       "Test Lead" },
    { "TP",       "Telephone Pole" },
    { "TPC",      "Telephone Pedestal" },
    { "TREEC",    "Tree (Coniferous)" },
    { "TREED",    "Tree (Deciduous)" },
    { "TRI",      "NGS Triangulation Mark" },
    { "TRLS",     "Transmission Line Structure" },
    { "TRMT",     "Tree Multiple Trunk" },
    { "TSCB",     "Traffic Signal Control Box" },
    { "TSG",      "Traffic Signal" },
    { "TSWA",     "Traffic Signal w/Arm" },
    { "TVP",      "TV Pedestal" },
    { "UME",      "Utility Marker Electric" },
    { "UMF",      "Fuel Utility Marker" },
    { "UMG",      "Gas Marker" },
    { "UMK",      "Unknown Utility Marker" },
    { "UMT",      "Telephone Utility Marker" },
    { "UMTV",     "TV Marker" },
    { "UMW",      "Water Utility Marker" },
    { "WELL",     "Well" },
    { "WF",       "Water Fountain" },
    { "WGV",      "Water Gate Valve" },
    { "WML",      "Wind Mill" },
    { "WMON",     "Well Mon Found" },
    { "WMTR",     "Water Meter" },
    { "WMV",      "Water Main Valve" },
    { "WSTA",     "Weather Station Fence or Item" },
    { "WSV",      "Water Service Valve" },
    { "WTG",      "Wind Turbine/Generator" },
    { "WVP",      "Water Vent Pipe" },

    /* --- Line codes (same workbook, "Line codes" section) --- */
    { "APAN",     "Bridge Approach Panel" },
    { "ASP",      "Asphalt" },
    { "BGB",      "Building Brick" },
    { "BGC",      "Building Conc./Mason" },
    { "BGF",      "Building Frame" },
    { "BGM",      "Building Metal" },
    { "BGMH",     "Mobile Home" },
    { "BGV",      "Building Vinyl" },
    { "BGW",      "Building Wood" },
    { "BR",       "Bridge Deck" },
    { "BRK",      "Break Line" },
    { "BXC",      "Concrete Box Culvert" },
    { "CAN",      "Canopy" },
    { "CIP",      "Cast Iron Pipe" },
    { "CIPD",     "Cast in Place Drain Concrete" },
    { "CL",       "Centerline of Road" },
    { "CLBANK",   "Channel Bank" },
    { "CON",      "Concrete" },
    { "CONJT",    "Concrete Joint" },
    { "CPP",      "Corrugated Plastic Pipe" },
    { "CRACK",    "Crack in Road" },
    { "CSP",      "Corrugated Steel Pipe" },
    { "CTGD",     "Cattle Guard" },
    { "CTOWL",    "Communications Tower (Line)" },
    { "DAME",     "Earth Dam" },
    { "DECK",     "Deck (HOUSE)" },
    { "DIK",      "Earth Dike/Berm" },
    { "DOK",      "Loading Dock" },
    { "DOORL",    "Door (Line)" },
    { "DRD",      "Drainage Ditch Unlined" },
    { "DRN",      "Drainage Profile/ Flow Line" },
    { "DRT",      "Dirt Road Trail" },
    { "DT",       "Drain Tile" },
    { "DWP",      "Detectable Warning Panel" },
    { "EDP",      "Exposed Drain Pipe" },
    { "EL",       "Electric Box" },
    { "ELVT",     "Electric Vault" },
    { "EOST",     "Edge of Steps" },
    { "EOW",      "Edge of Water" },
    { "FC",       "Face of Curb" },
    { "FL",       "Flowline" },
    { "FNB",      "Barb Wire" },
    { "FNC",      "Chain Link" },
    { "FNE",      "Electric" },
    { "FNV",      "Vinyl" },
    { "FNW",      "Wood Fence" },
    { "FNWW",     "Woven Wire Fence/Sheep Fence" },
    { "FPI",      "Fuel Dispenser Island" },
    { "GATE",     "Gate" },
    { "GDR",      "Guard Rail" },
    { "GM",       "Gas Line" },
    { "GRTD",     "Grated Top for CIPD" },
    { "GRV",      "Gravel" },
    { "GUY",      "Guy Wire (Physical Wire)" },
    { "GY",       "Gy Wire (Join to Power Pole with Control Code)" },
    { "HCR",      "Handicapped Ramp (STRUCTURE)" },
    { "HG",       "Hedge" },
    { "HWC",      "HeadWall Conc" },
    { "JB",       "Jersey Barrier" },
    { "LAK",      "Lake or Pond" },
    { "LFST",     "Lift Station" },
    { "LND",      "Landscape Edge" },
    { "LSTL",     "Low Steel (Bridges)" },
    { "MISCL",    "Misc. Line" },
    { "OH",       "Overhead Line" },
    { "OHW",      "Overhead Wire (Physical Wire)" },
    { "OROCK",    "Ornamental Rock (Line)" },
    { "PI",       "Point of Intersection" },
    { "PIER",     "Pier (Bridge Structure Only)" },
    { "PLO",      "Petrolium Pipeline" },
    { "PLTR",     "Planter" },
    { "PSB",      "Paint Stripe Blue" },
    { "PSW",      "Paint Stripe White" },
    { "PSY",      "Paint Stripe Yellow" },
    { "PVC",      "PVC Pipe" },
    { "RCP",      "Reinforced Conc. Pipe" },
    { "RCPA",     "Reinforced Arch Concrete Pipe" },
    { "RIP",      "Rip Rap" },
    { "RR",       "Railroad Tracks" },
    { "RRBRK",    "RR Profile Break Line" },
    { "RRXC",     "Railroad Xing Concrete" },
    { "RRXR",     "Railroad Xing Rubber" },
    { "RRXT",     "Railroad Xing Timber" },
    { "RTWB",     "Retaining Wall Bottom" },
    { "RTWC",     "Concrete Retaining Wall" },
    { "RTWR",     "Mason/Landscape Retaining Wall" },
    { "RTWT",     "Retaining Wall Top" },
    { "SAN",      "Sanitary Sewer Line" },
    { "SCALE",    "Weigh Station Scale Platform" },
    { "SDWK",     "Sidewalk" },
    { "SEP",      "Septic Tank" },
    { "SGNOH",    "Overhead Sign Structure" },
    { "SGNS",     "Traffic Sign (with 2 or more Posts)" },
    { "SHLD",     "Shoulder of Road" },
    { "SIGNS",    "Private Sign (2 or more Posts)" },
    { "SLIDE",    "Playground Slide" },
    { "SLO",      "Slough" },
    { "SLTDRN",   "Slotted Metal Drain" },
    { "SPLE",     "Earth Spillway" },
    { "SS",       "Storm Sewer Outlet" },
    { "STM",      "Steam Line" },
    { "STO",      "Storm Sewer Line" },
    { "SWSET",    "Playground Swing Set" },
    { "SWT",      "Railroad Switch" },
    { "TBANK",    "Top of Bank" },
    { "TBC",      "Top Back Curb" },
    { "TBO",      "Telephone Booth" },
    { "TFO",      "Telephone Fiber Optic" },
    { "TNKA",     "Above Ground Tank" },
    { "TNKP",     "Propane Tank (Line)" },
    { "TNKU",     "Under Ground Tank" },
    { "TNKW",     "Livestock Water Tank" },
    { "TOE",      "Toe of Slope" },
    { "TOP",      "Top of Slope" },
    { "TRM",      "Tree Mass (Measured at Canopy Edge)" },
    { "TSS",      "Traffic Signal Sensor" },
    { "TV",       "TV Cable" },
    { "TVFO",     "Fiber Optic" },
    { "TVLT",     "Telephone Vault" },
    { "TW",       "Tracer Wire" },
    { "UEC",      "Underground Electric" },
    { "UMC",      "Missle Cable" },
    { "WET",      "Wetlands" },
    { "WING",     "Wing Wall" },
    { "WTR",      "Water Line" },
    { "XING",     "Railroad Xing Unidentified" },
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