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
 *   - UNLOG is sent as the first init command to silence continuous NMEA
 *     output from a previous session before any other commands are sent.
 */

#define _GNU_SOURCE

#include "um980.h"
#include "stream/serial.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>

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

    /* Flush RX buffer before sending — discard any backlogged NMEA */
    tcflush(u->serial.fd, TCIFLUSH);

    SerialResult wr = serial_write(&u->serial, (const uint8_t *)packet, cmd_len + 2);
    if (wr != SERIAL_OK) {
        return wr;
    }

    /*
     * Read response lines until we see "OK" or "ERROR".
     * After tcflush the buffer is clean so 64 lines is sufficient.
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
    }

    return SERIAL_ERR_TIMEOUT;
}

/* -------------------------------------------------------------------------
 * um980_init_base
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

    SEND(u, "UNLOG");
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

SerialResult um980_init_rover(Um980 *u) {
    if (!u) {
        return SERIAL_ERR_ARG;
    }

    SEND(u, "UNLOG");
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