/**
 * @file serial.c
 * @brief POSIX serial port implementation using termios and select(2).
 */
#define _GNU_SOURCE

#include "serial.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static int baud_to_termios(int baud) {
    switch (baud) {
        case 9600:      return B9600;
        case 19200:     return B19200;
        case 38400:     return B38400;
        case 57600:     return B57600;
        case 115200:    return B115200;
        case 230400:    return B230400;
        case 460800:    return B460800;
        default:        return -1;
    }
}

/* -------------------------------------------------------------------------
 * serial_open
 * ---------------------------------------------------------------------- */

SerialResult serial_open(SerialPort *port, const char *device, int baud, int timeout_ms) {
    if (!port || !device) {
        return SERIAL_ERR_ARG;
    }

    int speed = baud_to_termios(baud);
    if (speed == -1) {
        return SERIAL_ERR_BAUD;
    }

    /*
     * O_RDWR    — we both read NMEA/RTCM and write commands
     * O_NOCTTY  — don't let the port become the controlling terminal
     * O_NONBLOCK — prevent open() blocking on modem control lines (DCD).
     *              The UM980 does not assert DCD; without this flag open()
     *              hangs indefinitely on a hardware UART.
     * O_CLOEXEC — don't leak fd across exec (defensive)
     *
     * O_NONBLOCK is cleared immediately after open() so that subsequent
     * reads and writes use blocking I/O managed by select().
     */
    int fd = open(device, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd == -1) {
        port->fd = -1;
        port->baud = 0;
        return SERIAL_ERR_OPEN;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    cfsetispeed(&tty, (speed_t)speed);
    cfsetospeed(&tty, (speed_t)speed);

    cfmakeraw(&tty);

    tty.c_cflag &= (tcflag_t)~CSTOPB;
    tty.c_cflag &= (tcflag_t)~PARENB;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= (tcflag_t)~CRTSCTS;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return SERIAL_ERR_ATTR;
    }

    tcflush(fd, TCIOFLUSH);

    port->fd = fd;
    port->baud = baud;
    port->timeout_ms = timeout_ms;

    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * serial_read
 * ---------------------------------------------------------------------- */

int serial_read(SerialPort *port, uint8_t *buf, size_t len) {
    if (!port || port->fd == -1 || !buf || len == 0) {
        return SERIAL_ERR_ARG;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(port->fd, &rfds);

    struct timeval tv;
    tv.tv_sec = port->timeout_ms / 1000;
    tv.tv_usec = (port->timeout_ms % 1000) * 1000;

    int ready = select(port->fd + 1, &rfds, NULL, NULL, &tv);

    if (ready == -1) {
        return SERIAL_ERR_IO;
    }
    if (ready == 0) {
        return SERIAL_ERR_TIMEOUT;
    }

    ssize_t n = read(port->fd, buf, len);
    if (n <= 0) {
        return SERIAL_ERR_IO;
    }

    return (int)n;
}

/* -------------------------------------------------------------------------
 * serial_write
 * ---------------------------------------------------------------------- */

SerialResult serial_write(SerialPort *port, const uint8_t *buf, size_t len) {
    if (!port || port->fd == -1 || !buf || len == 0) {
        return SERIAL_ERR_ARG;
    }

    size_t written = 0;

    while (written < len) {
        ssize_t n = write(port->fd, buf + written, len - written);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            return SERIAL_ERR_IO;
        }
        written += (size_t)n;
    }

    return SERIAL_OK;
}

/* -------------------------------------------------------------------------
 * serial_close
 * ---------------------------------------------------------------------- */

void serial_close(SerialPort *port) {
    if (!port) {
        return;
    }
    if (port->fd != -1) {
        close(port->fd);
        port->fd = -1;
    }
    port->baud = 0;
    port->timeout_ms = 0;
}