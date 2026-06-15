/**
 * @file um980.h
 * @brief Unicorecomm UM980 GNSS module interface.
 *
 * Wraps serial I/O with UM980-specific initialization sequences
 * for base station and rover modes.
 *
 * Command protocol (UM980 user manual §4):
 *   - Commands are ASCII, terminated with \r\n
 *   - Responses are ASCII lines: "$command,OK*xx\r\n" or "$command,ERROR*xx\r\n"
 *   - GeoMark always re-initializes the module on startup; SAVECONFIG is
 *     never called so flash wear and stale config are both avoided.
 */
#ifndef GEOMARK_UM980_H
#define GEOMARK_UM980_H
#include "../stream/serial.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
/** UM980 factory default baud rate (datasheet §3.2). */
#define UM980_BAUD 115200
/**
 * Timeout waiting for a command response from the UM980, in milliseconds.
 * The module typically responds within 100 ms; 2000 ms is a safe margin for
 * most commands. MODE BASE TIME requires extra time to process survey-in
 * parameters — um980_init_base() opens a dedicated serial port with a longer
 * timeout for that command specifically.
 */
#define UM980_TIMEOUT_MS 2000
#define UM980_TIMEOUT_MS_LONG 8000
/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */
typedef struct {
    SerialPort serial;
} Um980;
/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
/**
 * @brief Open the UM980 on the given device at 115200 baud.
 *
 * @param u       Caller-allocated Um980 struct.
 * @param device  Device path, e.g. "/dev/ttyAMA0".
 * @return        SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult um980_open(Um980 *u, const char *device);
/**
 * @brief Send an ASCII command and wait for an "OK" response.
 *
 * Appends \r\n, writes to the UM980, then reads response lines until
 * a line containing "OK" is seen (success) or the timeout elapses
 * or a line containing "ERROR" is seen (failure).
 *
 * @param u    Open Um980.
 * @param cmd  NULL-terminated ASCII command, without trailing \r\n.
 * @return     SERIAL_OK on success, SERIAL_ERR_IO on ERROR response,
 *             SERIAL_ERR_TIMEOUT if no response within UM980_TIMEOUT_MS.
 */
SerialResult um980_send_command(Um980 *u, const char *cmd);
/**
 * @brief Configure the UM980 for base station operation.
 *
 * Sends: MODE BASE TIME 60 2 2.5, CONFIG SIGNALGROUP 2,
 * RTCM1005/1077/1087/1097/1127.
 *
 * @param u  Open Um980.
 * @return   SERIAL_OK if all commands acknowledged, negative on first failure.
 */
SerialResult um980_init_base(Um980 *u);
/**
 * @brief Configure the UM980 for rover operation.
 *
 * Sends: MODE ROVER, CONFIG SIGNALGROUP 2, GPGGA 0.2, GPRMC 0.2.
 *
 * @param u  Open Um980.
 * @return   SERIAL_OK if all commands acknowledged, negative on first failure.
 */
SerialResult um980_init_rover(Um980 *u);
/**
 * @brief Close the UM980 serial port and reset the struct.
 *
 * Safe to call on an already-closed Um980.
 */
void um980_close(Um980 *u);
#ifdef __cplusplus
}
#endif
#endif /* GEOMARK_UM980_H */