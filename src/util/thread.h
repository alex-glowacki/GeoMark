/**
 * @file thread.h
 * @brief pthreads wrappers for station concurrency.
 */

#ifndef GEOMARK_THREAD_H
#define GEOMARK_THREAD_H

#include <pthread.h>

#include "geomark.h"

typedef pthread_t gm_thread_t;
typedef pthread_mutex_t gm_mutex_t;

gm_status_t thread_create(gm_thread_t *t, void *(*func)(void *), void *arg);
gm_status_t thread_join(gm_thread_t *t);
void mutex_init(gm_mutex_t *m);
void mutex_lock(gm_mutex_t *m);
void mutex_unlock(gm_mutex_t *m);
void mutex_destroy(gm_mutex_t *m);

#endif /* GEOMARK_THREAD_H */