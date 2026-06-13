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
 */

#define _GNU_SOURCE

#include "um980.h"
#include "stream/serial.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/**
 * Maximum length of a single UM980 response line, including \r\n.
 * Real responses are typically ~32 bytes; 128 is a safe upper bound.
 */
#define RESP_BUF_SIZE 128

/**
 * Read bytes from the serial port into @p out until a newline ('\n') is
 * seen, the buffer is full (leaving room for NUL), or the port times out.
 *
 * @param port      Open SerialPort.
 * @param out       Caller buffer, at least RESP_BUF_SIZE bytes.
 * @param out_size  Size of @p out.
 * @return          Number of bytes written into @p out (>= 0),
 *                  or negative SerialResult on read error/timeout.
 */
static int read_line(SerialPort *port, char *out, size_t out_size) {
    size_t pos = 0;

    while (pos < out_size - 1) {
        uint8_t byte;
        int n = serial_read(port, &byte, 1);

        if (n == SERIAL_ERR_TIMEOUT) {
            break; /* no more data within timeout */
        }
        if (n < 0) {
            return n; /* propagate IO error */
        }

        out[pos++] = (char)byte;

        if (byte == '\n') {
            break; /* end of response line */
        }
    }

    out[pos] = '\0';
    return (int)pos;
}

/* -------------------------------------------------------------------------
 * um980_open
 * ---------------------------------------------------------------------- */

SerialResult um980_open(Um980 *u, const char *device) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }
    return serial_open(&u->serial, device, UM980_BAUD, UM980_TIMEOUT_MS);
}

/* -------------------------------------------------------------------------
 * um980_send_command
 * ---------------------------------------------------------------------- */

SerialResult um980_send_command(Um980 *u, const char *cmd) {
    if (!u || !cmd) {
        return SERIAL_ERR_ARG;
    }

    /*
     * Build "<cmd>\r\n" in a fixed buffer.
     * UM980 commands are short ASCII strings; 128 bytes is sufficient.
     */
    char packet[128];
    size_t cmd_len = strlen(cmd);

    if (cmd_len + 2 >= sizeof(packet)) {
        return SERIAL_ERR_ARG; /* command too long */
    }

    memcpy(packet, cmd, cmd_len);
    packet[cmd_len]     = '\r';
    packet[cmd_len + 1] = '\n';
    packet[cmd_len + 2] = '\0';

    /* Write the command */
    SerialResult wr = serial_write(&u->serial, (const uint8_t *)packet, cmd_len + 2);
    if (wr != SERIAL_OK) {
        return wr;
    }

    /*
     * Read response lines until we see "OK" or "ERROR", or the port
     * times out. The UM980 streams unsolicited NMEA lines before the ACK,
     * so we scan up to 64 lines to ensure the response is not missed
     * regardless of NMEA output rate.
     */
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
        /* else: unsolicited line (NMEA etc.) — keep reading */
    }

    return SERIAL_ERR_TIMEOUT; /* gave up after 64 lines */
}

/* -------------------------------------------------------------------------
 * um980_init_base
 * ---------------------------------------------------------------------- */

/*
 * Base station command sequence (UM980 user manual §4):
 *
 *   MODE BASE          — enter base station mode
 *   CONFIG SIGNALGROUP 2
 *                      — enable L1/L2/L5 across GPS/BDS/GLONASS/Galileo
 *   RTCM1005 30        — stationary ARP message, every 30 s
 *   RTCM1077 1         — GPS MSM7, every 1 s
 *   RTCM1087 1         — GLONASS MSM7, every 1 s
 *   RTCM1097 1         — Galileo MSM7, every 1 s
 *   RTCM1127 1         — BeiDou MSM7, every 1 s
 *
 * MSM7 (full pseudorange + phase + CNR) is the highest-resolution RTCM3
 * format; required for cm-level RTK with the rover's UM980.
 */

/* Helper macro — send one command and return immediately on failure. */
#define SEND(u, cmd)                                        \
    do {                                                    \
        SerialResult _r = um980_send_command((u), (cmd));   \
        if (_r != SERIAL_OK) return _r;                     \
    } while (0)

SerialResult um980_init_base(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SEND(u, "MODE BASE");
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

/*
 * Rover command sequence:
 *
 *   MODE ROVER         — enter rover mode (accepts RTCM corrections)
 *   CONFIG SIGNALGROUP 2
 *   GPGGA 0.2          — GGA position fix at 5 Hz
 *   GPRMC 0.2          — RMC speed/course at 5 Hz
 *
 * 5 Hz (0.2 s interval) matches typical RTK update rates and keeps the
 * TFT display responsive without saturating the SiK radio link.
 */

SerialResult um980_init_rover(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SEND(u, "MODE ROVER");
    SEND(u, "CONFIG SIGNALGROUP 2");
    SEND(u, "GPGGA 0.2");
    SEND(u, "GPRMC 0.2");

    return SERIAL_OK;
}

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