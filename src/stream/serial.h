/**
 * @file serial.h
 * @brief POSIX serial port interface for UART communication with the UM980.
 *
 * Wraps open/read/write over a raw termios file descriptor.
 * All functions are non-reentrant; the caller owns the SerialPort struct
 * and is responsible for calling serial_close() when done.
 *
 * Baud rate is passed as a plain integer (e.g. 115200); the implementation
 * maps it to the appropriate Bxx constant internally.
 */

#ifndef GEOMARK_SERIAL_H
#define GEOMARK_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/** Opaque serial port handle. Zero-initialise before use. */
typedef struct {
    int fd;         /**< File descriptor; -1 when not open. */
    int baud;       /**< Configured baud rate (e.g. 115200). */
    int timeout_ms; /**< Read timeout passed to select(2), in milliseconds. */
} SerialPort;

/** Return codes shared across serial_* functions. */
typedef enum {
    SERIAL_OK = 0,          /**< Success. */
    SERIAL_ERR_ARG = -1,    /**< NULL pointer or invalid argument. */
    SERIAL_ERR_OPEN = -2,   /**< Failed to open device (check errno). */
    SERIAL_ERR_BAUD = -3,   /**< Unsupported baud rate. */
    SERIAL_ERR_ATTR = -4,   /**< termios get/set failed (check errno). */
    SERIAL_ERR_IO = -5,     /**< read()/write() error (check errno). */
    SERIAL_ERR_TIMEOUT = -6 /**< Read timed out with no data. */
} SerialResult;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Open and configure a serial port.
 *
 * Opens @p device, configures it for 8N1 raw mode at @p baud, no flow
 * control, and sets a read timeout of @p timeout_ms milliseconds via
 * select(2) (not VTIME — VTIME has 100 ms granularity).
 *
 * Supported baud rates: 9600, 19200, 38400, 57600, 115200, 230400, 460800.
 *
 * @param port        Caller-allocated SerialPort struct (need not be zeroed).
 * @param device      Device path, e.g. "/dev/ttyAMA0" or "/dev/ttyUSB0".
 * @param baud        Baud rate integer, e.g. 115200.
 * @param timeout_ms  Read timeout in milliseconds (0 = non-blocking poll).
 * @return            SERIAL_OK on success, negative SerialResult on error.
 */
SerialResult serial_open(SerialPort *port, const char *device, int baud, int timeout_ms);

/**
 * @brief Read up to @p len bytes from the serial port.
 *
 * Blocks up to the timeout configured in serial_open(). Returns as soon
 * as any data is available — does not wait for @p len bytes.
 *
 * @param port    Open SerialPort.
 * @param buf     Destination buffer.
 * @param len     Maximum bytes to read.
 * @return        Number of bytes read (>= 1), or negative SerialResult.
 *                SERIAL_ERR_TIMEOUT if the timeout elapsed with no data.
 */
int serial_read(SerialPort *port, uint8_t *buf, size_t len);

/**
 * @brief Write @p len bytes to the serial port.
 *
 * Blocking write; retries on short writes until all bytes are sent
 * or an error occurs.
 *
 * @param port    Open SerialPort.
 * @param buf     Source buffer.
 * @param len     Number of bytes to write.
 * @return        SERIAL_OK on success, SERIAL_ERR_IO on error.
 */
SerialResult serial_write(SerialPort *port, const uint8_t *buf, size_t len);

/**
 * @brief Close the serial port and reset the struct.
 *
 * Safe to call on an already-closed port (fd == -1).
 *
 * @param port    SerialPort to close.
 */
void serial_close(SerialPort *port);

#ifdef __cplusplus
}
#endif

#endif /* GEOMARK_SERIAL_H */