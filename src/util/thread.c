/**
 * @file thread.c
 * @brief pthreads wrapper implementation.
 */

#include "util/thread.h"
#include "util/log.h"

gm_status_t thread_create(gm_thread_t *t, void *(*func)(void *), void *arg)
{
    if (pthread_create(t, NULL, func, arg) != 0) {
        log_error("thread_create failed");
        return GM_ERR_GENERIC;
    }
    return GM_OK;
}

gm_status_t thread_join(gm_thread_t *t)
{
    if (pthread_join(*t, NULL) != 0) {
        log_error("thread_join failed");
        return GM_ERR_GENERIC;
    }
    return GM_OK;
}

void mutex_init(gm_mutex_t *m)    { pthread_mutex_init(m, NULL); }
void mutex_lock(gm_mutex_t *m)    { pthread_mutex_lock(m); }
void mutex_unlock(gm_mutex_t *m)  { pthread_mutex_unlock(m); }
void mutex_destroy(gm_mutex_t *m) { pthread_mutex_destroy(m); }