/**
 * @file um980.h
 * @brief Unicorecomm UM980 GNSS module interface.
 *
 * Wraps serial I/O with UM980-specific initialization sequences
 * for base station and rover modes.
 */

#ifndef GEOMARK_UM980_H
#define GEOMARK_UM980_H

#include "../stream/serial.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SerialPort serial;
} Um980;

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
 * Appends \r\n to the command string before sending.
 *
 * @param u    Open Um980.
 * @param cmd  NULL-terminated ASCII command string.
 * @return     SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult um980_send_command(Um980 *u, const char *cmd);

/**
 * @brief Configure the UM980 for base station operation.
 *
 * Sends MODE BASE, enables RTCM output messages.
 *
 * @param u  Open Um980.
 * @return   SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult um980_init_base(Um980 *u);

/**
 * @brief Configure the UM980 for rover operation.
 *
 * Sends MODE ROVER, enables NMEA output messages.
 *
 * @param u  Open Um980.
 * @return   SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult um980_init_rover(Um980 *u);

/**
 * @brief Close the UM980 serial port and reset the struct.
 */
void um980_close(Um980 *u);

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_UM980_H */