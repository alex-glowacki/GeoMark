/**
 * @file net/stream_client.c
 * @brief TCP client: receives RoverStatusPacket from the pole-top unit.
 */

#define _GNU_SOURCE

#include "net/stream_client.h"
#include "util/log.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* --------------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */

typedef struct {
    char host[256];
    StreamClientCallback callback;
    void *user;
} ClientCtx;

static ClientCtx g_ctx;
static pthread_t g_recv_tid;
static volatile int g_running = 0;

/* --------------------------------------------------------------------------
 * Receive thread
 * ----------------------------------------------------------------------- */

static void *recv_thread(void *arg) {
    (void)arg;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", ROVER_PACKET_PORT);

    while (g_running) {
        /* --- Resolve and connect ---------------------------------------- */
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo *res = NULL;
        int rc = getaddrinfo(g_ctx.host, port_str, &hints, &res);
        if (rc != 0) {
            log_warn("stream_client: getaddrinfo(%s): %s", g_ctx.host, gai_strerror(rc));
            sleep(2);
            continue;
        }

        int fd = socket(res->ai_family, res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
        if (fd < 0) {
            log_warn("stream_client: socket() failed: %s", strerror(errno));
            freeaddrinfo(res);
            sleep(2);
            continue;
        }

        if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
            log_info("stream_client: connect(%s:%s) failed: %s — retrying",
                    g_ctx.host, port_str, strerror(errno));
            close(fd);
            freeaddrinfo(res);
            sleep(2);
            continue;
        }
        freeaddrinfo(res);
        log_info("stream_client: connected to %s:%s", g_ctx.host, port_str);

        /* --- Receive loop ----------------------------------------------- */
        while (g_running) {
            RoverStatusPacket pkt;
            uint8_t *buf = (uint8_t *)&pkt;
            size_t remaining = sizeof(pkt);

            /* Read exactly sizeof(pkt) bytes */
            while (remaining > 0 && g_running) {
                ssize_t n = read(fd, buf, remaining);
                if (n <= 0) {
                    if (n == 0)
                        log_info("stream_client: server closed connection");
                    else
                        log_warn("stream_client: read() failed: %s",
                                strerror(errno));
                    goto disconnect;
                }
                buf += (size_t)n;
                remaining -= (size_t)n;
            }

            if (!g_running)
                break;

            /* Validate magic */
            if (pkt.magic != ROVER_PACKET_MAGIC) {
                log_warn("stream_client: bad magic 0x%08X — reconnecting", pkt.magic);
                goto disconnect;
            }

            g_ctx.callback(&pkt, g_ctx.user);
        }

    disconnect:
        close(fd);
        if (g_running)
            sleep(2);   /* brief pause before reconnect */
    }

    return NULL;
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

gm_status_t stream_client_start(const char *host, StreamClientCallback callback, void *user) {
    strncpy(g_ctx.host, host, sizeof(g_ctx.host) - 1);
    g_ctx.host[sizeof(g_ctx.host) - 1] = '\0';
    g_ctx.callback = callback;
    g_ctx.user = user;

    g_running = 1;

    if (pthread_create(&g_recv_tid, NULL, recv_thread, NULL) != 0) {
        log_error("stream_client: pthread_create failed: %s", strerror(errno));
        g_running = 0;
        return GM_ERR_IO;
    }

    log_info("stream_client: started, connecting to %s:%d", host, ROVER_PACKET_PORT);
    return GM_OK;
}

void stream_client_stop(void) {
    g_running = 0;
    pthread_join(g_recv_tid, NULL);
    log_info("stream_client: stopped");
}