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
        return wr;
    }

    char resp[RESP_BUF_SIZE];

    for (int attempt = 0; attempt < 64; attempt++) {
        int n = read_line(&u->serial, resp, sizeof(resp));

        if (n == SERIAL_ERR_TIMEOUT || n == 0) {
            return SERIAL_ERR_TIMEOUT;
        }
        if (n < 0) {
            return SERIAL_ERR_IO;
        }

        if (strstr(resp, "OK") != NULL) {
            return SERIAL_OK;
        }
        if (strstr(resp, "ERROR") != NULL) {
            return SERIAL_ERR_IO;
        }
    }

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
 * Per-constellation ephemeris, ONNEW (output whenever a new/updated
 * ephemeris is decoded, not continuously -- each satellite's ephemeris
 * changes roughly every two hours, so ONNEW is the correct cadence, not
 * ONTIME). Command names are confirmed against the SHORT form Unicore's
 * own manual table of contents uses (GPSEPH/GLOEPH/GALEPH/BDSEPH/
 * BD3EPH/QZSSEPH, "B" suffix appended for binary output per that same
 * manual's documented convention for RANGECMP -> RANGECMPB) -- NOT
 * verified against the full command reference body itself (only the
 * TOC was available), and a NovAtel-compatible source describing the
 * same message-ID family uses the longer form "GPSEPHEMB" instead.
 * um980_send_command() already surfaces a clear SERIAL_ERR_IO with the
 * exact rejected command logged if any one of these names is wrong for
 * the receiver's actual firmware -- staticlog/station.h's own doc
 * comment tells the caller to run a short (few-minute) test capture
 * before committing to a multi-hour occupation, specifically so a
 * command-name mismatch is caught immediately rather than discovered
 * after driving home.
 * ---------------------------------------------------------------------- */

#define SEND_STATIC(u, cmd)                                          \
    do {                                                            \
        SerialResult _r = um980_send_command((u), (cmd));           \
        if (_r != SERIAL_OK) {                                      \
            log_error("um980_init_static_log: command '%s' was "   \
                     "rejected or timed out (%d) -- check the "    \
                     "exact ephemeris log name for this firmware", \
                     (cmd), _r);                                    \
            return _r;                                              \
        }                                                           \
    } while (0)

SerialResult um980_init_static_log(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SerialResult r = send_unlog(u);
    if (r != SERIAL_OK) return r;

    SEND_STATIC(u, "CONFIG SIGNALGROUP 2");   /* all bands, same as base/rover */
    SEND_STATIC(u, "LOG RANGECMPB ONTIME 0.5");
    SEND_STATIC(u, "LOG GPSEPHB ONNEW");
    SEND_STATIC(u, "LOG GLOEPHB ONNEW");
    SEND_STATIC(u, "LOG GALEPHB ONNEW");
    SEND_STATIC(u, "LOG BDSEPHB ONNEW");
    SEND_STATIC(u, "LOG BD3EPHB ONNEW");
    SEND_STATIC(u, "LOG QZSSEPHB ONNEW");

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