/**
 * @file um980.c
 * @brief Unicorecomm UM980 GNSS module implementation.
 *
 * Command protocol notes:
 *   - Every command is terminated with \r\n before sending.
 *   - The UM980 response format observed on hardware:
 *       "$command,<CMD>,response: OK*<checksum>\r\n"   — accepted
 *       "$command,<CMD>,response: ERROR*<checksum>\r\n" — rejected
 *   - We scan for the substring "OK" or "ERROR" (not ",OK"/",ERROR")
 *     to be robust to response format variations.
 *   - We do not call SAVECONFIG. GeoMark re-initializes the UM980 on
 *     every startup so the active config always matches the binary.
 *   - UNLOG is sent first without waiting for OK — we simply wait 2000ms
 *     for the stream to stop then flush, guaranteeing a clean channel
 *     for all subsequent commands regardless of prior output rate.
 *   - MODE BASE TIME 60 starts survey-in. The UM980 never explicitly
 *     signals survey-in completion via UART, so RTCM3 output is enabled
 *     immediately after. The module refines the base position in the
 *     background while corrections are already flowing.
 */

#define _GNU_SOURCE

#include "um980.h"
#include "stream/serial.h"
#include "util/log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

#define RESP_BUF_SIZE 128

static int read_line(SerialPort *port, char *out, size_t out_size) {
    size_t pos = 0;

    while (pos < out_size - 1) {
        uint8_t byte;
        int n = serial_read(port, &byte, 1);

        if (n == SERIAL_ERR_TIMEOUT) {
            break;
        }
        if (n < 0) {
            return n;
        }

        out[pos++] = (char)byte;

        if (byte == '\n') {
            break;
        }
    }

    out[pos] = '\0';
    return (int)pos;
}

/**
 * @brief Send UNLOG to silence all UM980 output.
 *
 * Does not wait for the OK response — instead waits 2000 ms for the
 * stream to stop then flushes the RX buffer. This guarantees a clean
 * channel regardless of how fast the UM980 was streaming.
 */
static SerialResult send_unlog(Um980 *u)
{
    const char *cmd = "UNLOG\r\n";
    SerialResult r = serial_write(&u->serial, (const uint8_t *)cmd, 7);
    if (r != SERIAL_OK)
        return r;
    usleep(2000000);  /* 2000 ms — UM980 stops streaming well within this */
    tcflush(u->serial.fd, TCIFLUSH);
    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * um980_open
 * ---------------------------------------------------------------------- */

SerialResult um980_open(Um980 *u, const char *device) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }
    SerialResult r = serial_open(&u->serial, device, UM980_BAUD, UM980_TIMEOUT_MS);
    if (r != SERIAL_OK) {
        return r;
    }
    /* Flush buffered NMEA the UM980 sent before we opened the port */
    tcflush(u->serial.fd, TCIOFLUSH);
    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * um980_send_command
 * ---------------------------------------------------------------------- */

SerialResult um980_send_command(Um980 *u, const char *cmd) {
    if (!u || !cmd) {
        return SERIAL_ERR_ARG;
    }

    char packet[128];
    size_t cmd_len = strlen(cmd);

    if (cmd_len + 2 >= sizeof(packet)) {
        return SERIAL_ERR_ARG;
    }

    memcpy(packet, cmd, cmd_len);
    packet[cmd_len]     = '\r';
    packet[cmd_len + 1] = '\n';
    packet[cmd_len + 2] = '\0';

    SerialResult wr = serial_write(&u->serial, (const uint8_t *)packet, cmd_len + 2);
    if (wr != SERIAL_OK) {
        log_error("um980_send_command: '%s' -- serial_write failed (%d)", cmd, wr);
        return wr;
    }

    char resp[RESP_BUF_SIZE];

    for (int attempt = 0; attempt < 64; attempt++) {
        int n = read_line(&u->serial, resp, sizeof(resp));

        if (n == SERIAL_ERR_TIMEOUT || n == 0) {
            log_error("um980_send_command: '%s' -- no response from the "
                     "device (timed out waiting for a line)", cmd);
            return SERIAL_ERR_TIMEOUT;
        }
        if (n < 0) {
            log_error("um980_send_command: '%s' -- serial_read failed (%d), "
                     "not a timeout or a device response", cmd, n);
            return SERIAL_ERR_IO;
        }

        if (strstr(resp, "OK") != NULL) {
            return SERIAL_OK;
        }
        if (strstr(resp, "ERROR") != NULL) {
            /* strip the trailing \r\n read_line() left in resp for a
             * cleaner one-line log message */
            size_t len = strlen(resp);
            while (len > 0 && (resp[len - 1] == '\n' || resp[len - 1] == '\r'))
                resp[--len] = '\0';
            log_error("um980_send_command: '%s' -- device rejected it: '%s'",
                     cmd, resp);
            return SERIAL_ERR_IO;
        }
    }

    log_error("um980_send_command: '%s' -- gave up after 64 lines with "
             "neither OK nor ERROR seen", cmd);
    return SERIAL_ERR_TIMEOUT;
}

/* -------------------------------------------------------------------------
 * um980_init_base
 *
 * MODE BASE TIME 60 — survey-in with 60 second minimum averaging window.
 * The UM980 never explicitly signals survey-in completion via UART, so
 * RTCM3 output is enabled immediately after the mode command. The module
 * refines the base position in the background while corrections flow.
 *
 * Uses UM980_TIMEOUT_MS_LONG for MODE BASE TIME since this command takes
 * longer to acknowledge than others.
 *
 * RTCM1005 30 — station coordinates, every 30 seconds
 * RTCM1077 1  — GPS MSM7, every second
 * RTCM1087 1  — GLONASS MSM7, every second
 * RTCM1097 1  — Galileo MSM7, every second
 * RTCM1127 1  — BeiDou MSM7, every second
 * ---------------------------------------------------------------------- */

#define SEND(u, cmd)                                        \
    do {                                                    \
        SerialResult _r = um980_send_command((u), (cmd));   \
        if (_r != SERIAL_OK) return _r;                     \
    } while (0)

SerialResult um980_init_base(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SerialResult r = send_unlog(u);
    if (r != SERIAL_OK) return r;

    /* Use extended timeout for MODE BASE TIME — takes longer to acknowledge. */
    u->serial.timeout_ms = UM980_TIMEOUT_MS_LONG;
    r = um980_send_command(u, "MODE BASE TIME 60");
    u->serial.timeout_ms = UM980_TIMEOUT_MS;
    if (r != SERIAL_OK) return r;

    SEND(u, "CONFIG SIGNALGROUP 2");
    SEND(u, "RTCM1005 30");
    SEND(u, "RTCM1077 1");
    SEND(u, "RTCM1087 1");
    SEND(u, "RTCM1097 1");
    SEND(u, "RTCM1127 1");

    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * um980_init_rover
 * ---------------------------------------------------------------------- */

SerialResult um980_init_rover(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SerialResult r = send_unlog(u);
    if (r != SERIAL_OK) return r;

    SEND(u, "MODE ROVER");
    SEND(u, "CONFIG SIGNALGROUP 2");
    SEND(u, "GPGGA 0.2");
    SEND(u, "GPRMC 0.2");

    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * um980_init_static_log
 *
 * Configures the UM980 to stream raw pseudorange/carrier-phase
 * observations plus per-constellation ephemeris, for a static
 * occupation later converted to RINEX (Unicore's own "Converter" tool,
 * part of UPrecise -- Windows-only, see staticlog/station.h's own doc
 * comment) and submitted to NGS OPUS. Deliberately does NOT send MODE
 * BASE or MODE ROVER -- this is not an RTK session, real-time position
 * is irrelevant, only the raw measurements matter, and those are
 * produced regardless of MODE. The module is left in whatever
 * standalone/autonomous mode it was already in (or its power-on
 * default if freshly started).
 *
 * RANGECMPB ONTIME 0.5 -- compressed pseudorange/carrier-phase
 * observations, twice a second. Confirmed against Unicore's own
 * reference commands manual (N4 High Precision Products), which gives
 * this exact command as its own worked example for enabling raw
 * observation output.
 *
 * Per-constellation ephemeris, ONTIME 120 (period, not event-triggered).
 * Two prior attempts at the trigger keyword both failed on real
 * hardware: "ONNEW" (fabricated, not a real Unicore keyword) and
 * "ONCHANGED" (real Unicore syntax per the manual's general Section 7
 * rule, but apparently not accepted for GPSEPHB specifically on this
 * firmware -- "ONCHANGED only applies to particular messages" per that
 * same manual, and ephemeris evidently isn't one of them). ONTIME 120
 * replaces both: confirmed against a real published working UM980
 * configuration for this exact purpose (raw logging for RTKLIB/RINEX,
 * OpenStreetMap diary by a UM980 user, listing "GPSEPHB 120 BDSEPHB
 * 120 BD3EPHB 120 GLOEPHB 120 GALEPHB 120" with no ONCHANGED/ONNEW at
 * all), AND it reuses the exact "LOG <NAME> ONTIME <seconds>" grammar
 * already proven successful on THIS hardware in THIS session for
 * RANGECMPB, rather than trying a third different, still-unverified
 * keyword. 120 seconds is redundant relative to how rarely ephemeris
 * actually changes (~every two hours), but that redundancy is the
 * point: it guarantees a fresh, valid nav message is always present in
 * the log regardless of when the RINEX conversion happens to sample
 * it, at the cost of a little extra file size.
 *
 * QZSSEPHB is not in that published working config (only GPS/BDS/
 * BDS-3/GLONASS/Galileo are) -- kept here since it's a real, TOC-listed
 * Unicore message name and QZSS ephemeris costs nothing extra to
 * request, but it's the one command in this list without independent
 * real-world confirmation. staticlog/station.h's own doc comment tells
 * the caller to run a short (few-minute) test capture before
 * committing to a multi-hour occupation for exactly this reason --
 * two real firmware rejections already came out of that habit this
 * session, at zero field-time cost.
 *
 * COMMAND ORDER MATTERS: RANGECMPB is sent LAST, after every other
 * command has already been sent and cleanly confirmed -- not
 * interleaved among the ephemeris commands, and not sent before them.
 * RANGECMPB ONTIME 0.5 starts a CONTINUOUS binary stream at 2 Hz the
 * instant it's acknowledged. Any command sent AFTER that has to have
 * its own plain-text "OK"/"ERROR" response parsed out of a serial
 * channel that's simultaneously carrying that live binary stream --
 * read_line() reads byte-by-byte hunting for a line ending, and binary
 * data landing in that window can burn through many of
 * um980_send_command()'s 64 retry attempts before a clean text line
 * surfaces, which looks exactly like a long hang rather than a clean
 * failure. Sending RANGECMPB last means nothing is ever parsed
 * concurrently with it -- every other command's response is read on an
 * otherwise-quiet channel, and once RANGECMPB itself is confirmed,
 * um980_init_static_log() returns immediately with no more commands
 * left to parse a response for.
 * ---------------------------------------------------------------------- */

#define SEND_STATIC(u, cmd)                                          \
    do {                                                            \
        SerialResult _r = um980_send_command((u), (cmd));           \
        if (_r != SERIAL_OK) {                                      \
            /* um980_send_command() itself already logged the exact \
             * reason (device ERROR response text, timeout, or a    \
             * low-level I/O error) -- this just identifies WHICH   \
             * of static-log's own commands failed. */              \
            log_error("um980_init_static_log: '%s' failed -- see "  \
                     "the line above for why", (cmd));              \
            return _r;                                              \
        }                                                            \
    } while (0)

SerialResult um980_init_static_log(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SerialResult r = send_unlog(u);
    if (r != SERIAL_OK) return r;

    SEND_STATIC(u, "CONFIG SIGNALGROUP 2");   /* all bands, same as base/rover */
    SEND_STATIC(u, "LOG GPSEPHB ONTIME 120");
    SEND_STATIC(u, "LOG GLOEPHB ONTIME 120");
    SEND_STATIC(u, "LOG GALEPHB ONTIME 120");
    SEND_STATIC(u, "LOG BDSEPHB ONTIME 120");
    SEND_STATIC(u, "LOG BD3EPHB ONTIME 120");
    SEND_STATIC(u, "LOG QZSSEPHB ONTIME 120");
    /* RANGECMPB LAST -- see this function's own doc comment on why
     * order matters here. Nothing is sent after this, so there is no
     * later command whose response could ever be parsed concurrently
     * with the continuous binary stream this starts. */
    SEND_STATIC(u, "LOG RANGECMPB ONTIME 0.5");

    return SERIAL_OK;
}

#undef SEND_STATIC
#undef SEND

/* -------------------------------------------------------------------------
 * um980_close
 * ---------------------------------------------------------------------- */

void um980_close(Um980 *u) {
    if (!u) {
        return;
    }
    serial_close(&u->serial);
}