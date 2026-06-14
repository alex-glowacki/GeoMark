/**
 * @file survey.h
 * @brief Survey session lifecycle, point capture, and averaging.
 *
 * One survey session = one CSV file.  The caller drives the state machine:
 *
 *   survey_session_open()
 *     loop:
 *       survey_capture_begin()
 *       survey_capture_feed()   -- called once per incoming GGA fix
 *       survey_capture_finish() -- blocks until window closes or aborts
 *     survey_session_close()
 */

#ifndef GEOMARK_SURVEY_H
#define GEOMARK_SURVEY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "geomark.h"

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/** Minimum fix quality accepted during capture (4 = RTK Fixed). */
#define SURVEY_MIN_FIX_QUALITY 4

/** Number of consecutive fixes to average per point. */
#define SURVEY_CAPTURE_FIXES 3

/** Maximum length of a point code string (including NUL). */
#define SURVEY_CODE_MAX 32

/** Maximum length of a description string (including NUL). */
#define SURVEY_DESC_MAX 128

/** Maximum number of points per session. */
#define SURVEY_POINTS_MAX 4096

/* --------------------------------------------------------------------------
 * A single captured survey point
 * -------------------------------------------------------------------------- */

typedef struct {
    double lat;                 /* decimal degrees, WGS-84              */
    double lon;                 /* decimal degrees, WGS-84              */
    double alt;                 /* metres above MSL                     */
    char code[SURVEY_CODE_MAX]; /* point code e.g. "CONC"               */
    char desc[SURVEY_DESC_MAX]; /* optional description                  */
    uint8_t fix_quality;        /* gm_fix_type_t value at capture time  */
    double hdop;
    uint8_t num_sats;
    time_t timestamp;   /* UTC epoch seconds                    */
    uint32_t point_num; /* 1-based index within session         */
} SurveyPoint;

/* --------------------------------------------------------------------------
 * Capture accumulator (internal use — exposed here for stack allocation)
 * -------------------------------------------------------------------------- */

typedef struct {
    double lat_sum;
    double lon_sum;
    double alt_sum;
    double hdop_sum;
    uint32_t sats_sum;
    uint8_t fix_quality; /* worst fix seen during window              */
    uint8_t count;       /* fixes accumulated so far                  */
    bool active;         /* capture in progress                       */
    bool aborted;        /* set if fix quality dropped below minimum  */
    char code[SURVEY_CODE_MAX];
    char desc[SURVEY_DESC_MAX];
} CaptureAccum;

/* --------------------------------------------------------------------------
 * Session
 * -------------------------------------------------------------------------- */

typedef struct {
    FILE *csv_file;                        /* open file handle              */
    char path[256];                        /* full path to CSV on disk      */
    char session_name[64];                 /* e.g. "20260614_143022"        */
    SurveyPoint points[SURVEY_POINTS_MAX]; /* completed points this session */
    uint32_t point_count;
    CaptureAccum accum; /* current capture window        */
    bool open;
} SurveySession;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * Open a new survey session and create the CSV file at @p path.
 * Writes the CSV header row immediately.
 *
 * @param session     Caller-allocated session struct (zeroed by this call).
 * @param path        Full path to the output CSV file.
 * @param name        Session name written into the header comment.
 * @return  0 on success, -1 on error (errno set).
 */
int survey_session_open(SurveySession *session, const char *path, const char *name);

/**
 * Close the session and flush the CSV file to disk.
 * Safe to call on an already-closed session.
 */
void survey_session_close(SurveySession *session);

/**
 * Begin a new capture window for one point.
 * Resets the accumulator and sets the point code and description.
 *
 * @param session  Open session.
 * @param code     Point code string (e.g. "CONC").
 * @param desc     Optional description (may be empty string, not NULL).
 * @return  0 on success, -1 if session not open or capture already active.
 */
int survey_capture_begin(SurveySession *session, const char *code, const char *desc);

/**
 * Feed one GGA fix into the active capture window.
 * Called from the stream_client receive callback at 1 Hz.
 *
 * @param session     Open session with active capture.
 * @param lat         Decimal degrees.
 * @param lon         Decimal degrees.
 * @param alt         Metres MSL.
 * @param fix_quality gm_fix_type_t value from the incoming packet.
 * @param hdop        Horizontal dilution of precision.
 * @param num_sats    Number of satellites.
 * @return  Number of fixes accumulated so far, or -1 if aborted due to
 *          fix quality drop.
 */
int survey_capture_feed(SurveySession *session, double lat, double lon, double alt,
                        uint8_t fix_quality, double hdop, uint8_t num_sats);

/**
 * Finish the capture window, average the accumulated fixes, write the CSV
 * row, and store the point in the session.
 *
 * @param session  Open session with a completed (not aborted) capture.
 * @param out      If non-NULL, receives a copy of the written point.
 * @return  0 on success, -1 if capture incomplete or aborted.
 */
int survey_capture_finish(SurveySession *session, SurveyPoint *out);

/**
 * Abort the current capture window without writing a point.
 * Resets the accumulator so a new capture can begin.
 */
void survey_capture_abort(SurveySession *session);

/**
 * Return true if the capture window has accumulated enough fixes and
 * has not been aborted.  The UI polls this to know when to call
 * survey_capture_finish().
 */
bool survey_capture_ready(const SurveySession *session);

/**
 * Return the number of fixes accumulated in the current window.
 * Returns 0 if no capture is active.
 */
int survey_capture_count(const SurveySession *session);

#endif /* GEOMARK_SURVEY_H */