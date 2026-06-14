/**
 * @file survey.c
 * @brief Survey session lifecycle, point capture, and averaging.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <string.h>
#include <time.h>

#include "survey/survey.h"
#include "survey/export.h"
#include "util/log.h"

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/** Zero the capture accumulator and mark it inactive. */
static void accum_reset(CaptureAccum *a)
{
    memset(a, 0, sizeof(*a));
    /* active and aborted are already false after memset */
}

/* --------------------------------------------------------------------------
 * Session open / close
 * -------------------------------------------------------------------------- */

int survey_session_open(SurveySession *session,
                        const char    *path,
                        const char    *name)
{
    memset(session, 0, sizeof(*session));

    session->csv_file = fopen(path, "w");
    if (!session->csv_file) {
        log_error("survey: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    strncpy(session->path,         path, sizeof(session->path) - 1);
    strncpy(session->session_name, name, sizeof(session->session_name) - 1);

    if (export_write_header(session->csv_file) != 0) {
        log_error("survey: failed to write CSV header to %s", path);
        fclose(session->csv_file);
        session->csv_file = NULL;
        return -1;
    }

    session->open = true;
    log_info("survey: session '%s' opened at %s", name, path);
    return 0;
}

void survey_session_close(SurveySession *session)
{
    if (!session->open)
        return;

    if (session->csv_file) {
        fflush(session->csv_file);
        fclose(session->csv_file);
        session->csv_file = NULL;
    }

    log_info("survey: session '%s' closed — %u point(s) collected",
             session->session_name, session->point_count);

    session->open = false;
}

/* --------------------------------------------------------------------------
 * Capture window
 * -------------------------------------------------------------------------- */

int survey_capture_begin(SurveySession *session,
                         const char    *code,
                         const char    *desc)
{
    if (!session->open) {
        log_warn("survey: capture_begin called on closed session");
        return -1;
    }
    if (session->accum.active) {
        log_warn("survey: capture_begin called while capture already active");
        return -1;
    }

    accum_reset(&session->accum);

    strncpy(session->accum.code, code, SURVEY_CODE_MAX - 1);
    session->accum.code[SURVEY_CODE_MAX - 1] = '\0';

    strncpy(session->accum.desc, desc, SURVEY_DESC_MAX - 1);
    session->accum.desc[SURVEY_DESC_MAX - 1] = '\0';

    /* Seed fix_quality to the best possible value — it can only get worse
     * as fixes are fed in.  Using FIX_RTK_FIXED (4) as the ceiling means
     * the first feed will set the real floor. */
    session->accum.fix_quality = FIX_RTK_FIXED;
    session->accum.active      = true;

    log_debug("survey: capture begun — code='%s' desc='%s'",
              session->accum.code, session->accum.desc);
    return 0;
}

int survey_capture_feed(SurveySession *session,
                        double         lat,
                        double         lon,
                        double         alt,
                        uint8_t        fix_quality,
                        double         hdop,
                        uint8_t        num_sats)
{
    if (!session->open || !session->accum.active)
        return -1;

    if (session->accum.aborted)
        return -1;

    /* Reject fix if quality is below the minimum threshold. */
    if (fix_quality < SURVEY_MIN_FIX_QUALITY) {
        log_warn("survey: fix quality %u dropped below minimum %u — "
                 "aborting capture", fix_quality, SURVEY_MIN_FIX_QUALITY);
        session->accum.aborted = true;
        return -1;
    }

    /* Accumulate. */
    session->accum.lat_sum  += lat;
    session->accum.lon_sum  += lon;
    session->accum.alt_sum  += alt;
    session->accum.hdop_sum += hdop;
    session->accum.sats_sum += num_sats;

    /* Track worst (lowest) fix quality seen in this window. */
    if (fix_quality < session->accum.fix_quality)
        session->accum.fix_quality = fix_quality;

    session->accum.count++;

    log_debug("survey: fix %u/%u — lat=%.9f lon=%.9f alt=%.3f q=%u",
              session->accum.count, SURVEY_CAPTURE_FIXES,
              lat, lon, alt, fix_quality);

    return (int)session->accum.count;
}

int survey_capture_finish(SurveySession *session, SurveyPoint *out)
{
    if (!session->open) {
        log_warn("survey: capture_finish called on closed session");
        return -1;
    }
    if (!session->accum.active) {
        log_warn("survey: capture_finish called with no active capture");
        return -1;
    }
    if (session->accum.aborted) {
        log_warn("survey: capture_finish called on aborted capture");
        accum_reset(&session->accum);
        return -1;
    }
    if (session->accum.count < SURVEY_CAPTURE_FIXES) {
        log_warn("survey: capture_finish called with only %u/%u fixes",
                 session->accum.count, SURVEY_CAPTURE_FIXES);
        return -1;
    }
    if (session->point_count >= SURVEY_POINTS_MAX) {
        log_error("survey: session point limit (%u) reached", SURVEY_POINTS_MAX);
        return -1;
    }

    /* Average the accumulated fixes. */
    double n = (double)session->accum.count;

    SurveyPoint pt;
    memset(&pt, 0, sizeof(pt));

    pt.lat         = session->accum.lat_sum  / n;
    pt.lon         = session->accum.lon_sum  / n;
    pt.alt         = session->accum.alt_sum  / n;
    pt.hdop        = session->accum.hdop_sum / n;
    pt.num_sats    = (uint8_t)(session->accum.sats_sum / session->accum.count);
    pt.fix_quality = session->accum.fix_quality; /* worst seen in window */
    pt.timestamp   = time(NULL);                 /* UTC epoch at finish  */
    pt.point_num   = session->point_count + 1;   /* 1-based              */

    strncpy(pt.code, session->accum.code, SURVEY_CODE_MAX - 1);
    pt.code[SURVEY_CODE_MAX - 1] = '\0';

    strncpy(pt.desc, session->accum.desc, SURVEY_DESC_MAX - 1);
    pt.desc[SURVEY_DESC_MAX - 1] = '\0';

    /* Write to CSV immediately — power-safe. */
    if (export_write_point(session->csv_file, &pt) != 0) {
        log_error("survey: failed to write point %u to CSV", pt.point_num);
        accum_reset(&session->accum);
        return -1;
    }

    /* Store in session array and advance counter. */
    session->points[session->point_count] = pt;
    session->point_count++;

    log_info("survey: point %u saved — %s '%s' lat=%.9f lon=%.9f alt=%.3fm",
             pt.point_num, pt.code, pt.desc, pt.lat, pt.lon, pt.alt);

    /* Hand copy to caller if requested. */
    if (out)
        *out = pt;

    accum_reset(&session->accum);
    return 0;
}

void survey_capture_abort(SurveySession *session)
{
    if (!session->accum.active)
        return;

    log_info("survey: capture aborted (had %u/%u fixes)",
             session->accum.count, SURVEY_CAPTURE_FIXES);

    accum_reset(&session->accum);
}

bool survey_capture_ready(const SurveySession *session)
{
    return session->accum.active
        && !session->accum.aborted
        && session->accum.count >= SURVEY_CAPTURE_FIXES;
}

int survey_capture_count(const SurveySession *session)
{
    if (!session->accum.active)
        return 0;
    return (int)session->accum.count;
}