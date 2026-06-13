/**
 * @file net/stream_server.c
 * @brief TCP server: streams RoverStatusPacket to the Pi 5 handheld at 2 Hz.
 */

#define _GNU_SOURCE

#include "net/stream_server.h"
#include "util/log.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/* --------------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */

static int g_listen_fd = -1;
static int g_client_fd = -1;
static pthread_t g_accept_tid;
static pthread_mutex_t g_client_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running = 0;

/* --------------------------------------------------------------------------
 * Accept thread — blocks on accept(), replaces current client
 * ----------------------------------------------------------------------- */

static void *accept_thread(void *arg) {
    (void)arg;

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (fd < 0) {
            if (!g_running)
                break;  /* shutdown in progress */
            log_warn("stream_server: accept() failed: %s", strerror(errno));
            continue;
        }

        /* Enable TCP_NODELAY — packets are small (44 bytes) and time-critical */
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        pthread_mutex_lock(&g_client_mutex);
        if (g_client_fd >= 0) {
            log_info("stream_server: replacing old client");
            close(g_client_fd);
        }
        g_client_fd = fd;
        pthread_mutex_unlock(&g_client_mutex);

        log_info("stream_server: client connected");
    }

    return NULL;
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

gm_status_t stream_server_start(void) {
    g_listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_listen_fd < 0) {
        log_error("stream_server: socket() failed: %s", strerror(errno));
        return GM_ERR_IO;
    }

    /* Allow fast restart after SIGINT */
    int one = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* 0.0.0.0 */
    addr.sin_port = htons(ROVER_PACKET_PORT);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("stream_server: bind() failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return GM_ERR_IO;
    }

    if (listen(g_listen_fd, 1) < 0) {
        log_error("stream_server: listen() failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return GM_ERR_IO;
    }

    g_running = 1;

    if (pthread_create(&g_accept_tid, NULL, accept_thread, NULL) != 0) {
        log_error("stream_server: pthread_create failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        g_running = 0;
        return GM_ERR_IO;
    }

    log_info("stream_server: listening on 0.0.0.0:%d", ROVER_PACKET_PORT);
    return GM_OK;
}

void stream_server_broadcast(const RoverStatusPacket *pkt) {
    pthread_mutex_lock(&g_client_mutex);

    if (g_client_fd < 0) {
        pthread_mutex_unlock(&g_client_mutex);
        return; /* no client connected — silently drop */
    }

    /* Write the full 44-byte packet atomically */
    const uint8_t *buf = (const uint8_t *)pkt;
    size_t remaining = sizeof(RoverStatusPacket);
    while (remaining > 0) {
        ssize_t n = write(g_client_fd, buf, remaining);
        if (n <= 0) {
            log_warn("stream_server: client write failed — disconnecting");
            close(g_client_fd);
            g_client_fd = -1;
            break;
        }
        buf += (size_t)n;
        remaining -= (size_t)n;
    }

    pthread_mutex_unlock(&g_client_mutex);
}

void stream_server_stop(void) {
    g_running = 0;

    /* Unblock accept() by closing the listen socket */
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    pthread_join(g_accept_tid, NULL);

    pthread_mutex_lock(&g_client_mutex);
    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }
    pthread_mutex_unlock(&g_client_mutex);

    log_info("stream_server: stopped");
}