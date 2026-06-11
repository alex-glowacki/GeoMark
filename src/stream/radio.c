/**
 * @file radio.h
 * @brief SiK 915 MHz telemetry radio interface (thin wrapper over serial).
 *
 * The SiK radio presents as a transparent UART bridge — no framing protocol
 * beyond what the serial layer provides. This module wraps SerialPort with
 * a radio-specific open call that sets the correct baud rate.
 */

#ifndef GEOMARK_RADIO_H
#define GEOMARK_RADIO_H

#include "../stream/serial.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SerialPort serial;
} Radio;

/**
 * @brief Open the SiK radio on the given device at the given baud rate.
 *
 * @param r       Caller-allocated Radio struct.
 * @param device  Device path, e.g. "/dev/ttyAMA1".
 * @param baud    Baud rate, e.g. 57600.
 * @return        SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult radio_open(Radio *r, const char *device, int baud);

/**
 * @brief Read up to @p len bytes from the radio.
 *
 * @param r    Open Radio.
 * @param buf  Destination buffer.
 * @param len  Maximum bytes to read.
 * @return     Bytes read (>= 1), or negative SerialResult.
 */
int radio_read(Radio *r, uint8_t *buf, size_t len);

/**
 * @brief Write @p len bytes to the radio.
 *
 * @param r    Open Radio.
 * @param buf  Source buffer.
 * @param len  Number of bytes to write.
 * @return     SERIAL_OK on success, SERIAL_ERR_IO on error.
 */
SerialResult radio_write(Radio *r, const uint8_t *buf, size_t len);

/**
 * @brief Close the radio and reset the struct.
 */
void radio_close(Radio *r);

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_RADIO_H */