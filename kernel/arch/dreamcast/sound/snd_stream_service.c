/* KallistiOS ##version##

   snd_stream_service.c
   Copyright (C) 2026 Joseph Black

   Explicit, caller-sized automatic sound-stream polling.
*/

#include <dc/sound/stream_service.h>

#include <kos/cond.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/timer.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "snd_stream_internal.h"

struct snd_stream_service {
    mutex_t mutex;
    condvar_t idle;
    semaphore_t work;
    kthread_t *thread;
    snd_stream_service_error_cb_t error_cb;
    void *user_data;
    uint32_t poll_interval_ms;
    snd_stream_service_state_t state;
    snd_stream_hnd_t active_stream;
    snd_stream_hnd_t last_error_stream;
    int last_error;
    bool registered[SND_STREAM_MAX];
    uint32_t registered_streams;
    uint64_t sweeps;
    uint64_t polls;
    uint64_t successful_polls;
    uint64_t underruns;
    uint64_t hard_errors;
};

static int require_thread_context(void) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    return 0;
}

static uint32_t deadline_remaining(uint64_t deadline) {
    uint64_t now = timer_ms_gettime64();
    uint64_t remaining;

    if(now >= deadline)
        return 0;

    remaining = deadline - now;
    return remaining > INT_MAX ? INT_MAX : (uint32_t)remaining;
}

static void service_release_streams(snd_stream_service_t *service) {
    snd_stream_hnd_t stream;

    for(stream = 0; stream < SND_STREAM_MAX; ++stream) {
        bool release;

        mutex_lock(&service->mutex);
        release = service->registered[stream];
        service->registered[stream] = false;
        if(release)
            --service->registered_streams;
        mutex_unlock(&service->mutex);

        if(release)
            (void)_snd_stream_service_release(stream, service);
    }
}

static void *stream_service_thread(void *data) {
    snd_stream_service_t *service = data;

    for(;;) {
        snd_stream_hnd_t stream;
        bool stopping;

        for(stream = 0; stream < SND_STREAM_MAX; ++stream) {
            snd_stream_service_error_cb_t error_cb = NULL;
            void *user_data = NULL;
            int poll_error = 0;
            bool underrun = false;
            int result;

            mutex_lock(&service->mutex);
            stopping = service->state != SND_STREAM_SERVICE_RUNNING;
            if(stopping || !service->registered[stream]) {
                mutex_unlock(&service->mutex);
                if(stopping)
                    break;
                continue;
            }
            service->active_stream = stream;
            mutex_unlock(&service->mutex);

            result = _snd_stream_service_poll(stream, service, &underrun);
            if(result < 0)
                poll_error = errno;

            mutex_lock(&service->mutex);
            ++service->polls;
            if(result == 0) {
                ++service->successful_polls;
            }
            else if(underrun) {
                ++service->underruns;
            }
            else if(poll_error != EAGAIN) {
                ++service->hard_errors;
                service->last_error_stream = stream;
                service->last_error = poll_error;
                error_cb = service->error_cb;
                user_data = service->user_data;
            }
            mutex_unlock(&service->mutex);

            if(error_cb)
                error_cb(service, stream, poll_error, user_data);

            mutex_lock(&service->mutex);
            service->active_stream = SND_STREAM_INVALID;
            cond_broadcast(&service->idle);
            mutex_unlock(&service->mutex);
        }

        mutex_lock(&service->mutex);
        stopping = service->state != SND_STREAM_SERVICE_RUNNING;
        if(!stopping)
            ++service->sweeps;
        mutex_unlock(&service->mutex);
        if(stopping)
            break;

        /* Posts make newly registered work visible immediately. A timeout
           supplies the normal cadence without a permanently runnable loop. */
        if(sem_wait_timed(&service->work, service->poll_interval_ms) < 0 &&
           errno != ETIMEDOUT) {
            int wait_error = errno;

            mutex_lock(&service->mutex);
            ++service->hard_errors;
            service->last_error_stream = SND_STREAM_INVALID;
            service->last_error = wait_error;
            mutex_unlock(&service->mutex);
        }
    }

    service_release_streams(service);

    mutex_lock(&service->mutex);
    service->active_stream = SND_STREAM_INVALID;
    service->state = SND_STREAM_SERVICE_STOPPED;
    cond_broadcast(&service->idle);
    mutex_unlock(&service->mutex);
    return NULL;
}

snd_stream_service_t *snd_stream_service_create(
    uint32_t poll_interval_ms, const kthread_attr_t *thread_attr,
    snd_stream_service_error_cb_t error_cb, void *user_data) {
    kthread_attr_t attr;
    snd_stream_service_t *service;
    int saved_errno;

    if(require_thread_context() < 0)
        return NULL;
    if(!poll_interval_ms || !thread_attr || !thread_attr->stack_size ||
       thread_attr->create_detached) {
        errno = EINVAL;
        return NULL;
    }

    service = calloc(1, sizeof(*service));
    if(!service) {
        errno = ENOMEM;
        return NULL;
    }

    if(mutex_init(&service->mutex, MUTEX_TYPE_NORMAL) < 0)
        goto fail_service;
    if(cond_init(&service->idle) < 0)
        goto fail_mutex;
    if(sem_init(&service->work, 0) < 0)
        goto fail_cond;

    service->error_cb = error_cb;
    service->user_data = user_data;
    service->poll_interval_ms = poll_interval_ms;
    service->state = SND_STREAM_SERVICE_RUNNING;
    service->active_stream = SND_STREAM_INVALID;
    service->last_error_stream = SND_STREAM_INVALID;

    attr = *thread_attr;
    if(!attr.label)
        attr.label = "[sound-streams]";
    service->thread = thd_create_ex(&attr, stream_service_thread, service);
    if(!service->thread)
        goto fail_sem;

    return service;

fail_sem:
    saved_errno = errno;
    sem_destroy(&service->work);
    errno = saved_errno;
fail_cond:
    saved_errno = errno;
    cond_destroy(&service->idle);
    errno = saved_errno;
fail_mutex:
    saved_errno = errno;
    mutex_destroy(&service->mutex);
    errno = saved_errno;
fail_service:
    saved_errno = errno;
    free(service);
    errno = saved_errno;
    return NULL;
}

int snd_stream_service_add(snd_stream_service_t *service,
                           snd_stream_hnd_t stream) {
    int result = -1;

    if(require_thread_context() < 0)
        return -1;
    if(!service || stream < 0 || stream >= SND_STREAM_MAX) {
        errno = EINVAL;
        return -1;
    }
    if(mutex_lock(&service->mutex) < 0)
        return -1;
    if(service->state != SND_STREAM_SERVICE_RUNNING) {
        errno = ENODEV;
        goto out;
    }
    if(service->registered[stream]) {
        errno = EALREADY;
        goto out;
    }
    if(_snd_stream_service_claim(stream, service) < 0)
        goto out;

    service->registered[stream] = true;
    ++service->registered_streams;
    sem_signal(&service->work);
    result = 0;

out:
    mutex_unlock(&service->mutex);
    return result;
}

int snd_stream_service_remove(snd_stream_service_t *service,
                              snd_stream_hnd_t stream,
                              uint32_t timeout_ms) {
    uint64_t deadline;
    int result = -1;

    if(require_thread_context() < 0)
        return -1;
    if(!service || stream < 0 || stream >= SND_STREAM_MAX || !timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(service->thread == thd_get_current()) {
        errno = EDEADLK;
        return -1;
    }

    deadline = timer_ms_gettime64() + timeout_ms;
    if(mutex_lock(&service->mutex) < 0)
        return -1;
    if(service->state != SND_STREAM_SERVICE_RUNNING) {
        errno = ENODEV;
        goto out;
    }
    if(!service->registered[stream]) {
        errno = ENOENT;
        goto out;
    }

    service->registered[stream] = false;
    --service->registered_streams;
    while(service->active_stream == stream) {
        uint32_t remaining = deadline_remaining(deadline);

        if(!remaining) {
            service->registered[stream] = true;
            ++service->registered_streams;
            errno = ETIMEDOUT;
            goto out;
        }
        if(cond_wait_timed(&service->idle, &service->mutex,
                           (int)remaining) < 0) {
            service->registered[stream] = true;
            ++service->registered_streams;
            goto out;
        }
    }

    result = _snd_stream_service_release(stream, service);
    if(result < 0) {
        service->registered[stream] = true;
        ++service->registered_streams;
    }

out:
    mutex_unlock(&service->mutex);
    return result;
}

int snd_stream_service_get_status(snd_stream_service_t *service,
                                  snd_stream_service_status_t *status) {
    if(require_thread_context() < 0)
        return -1;
    if(!service || !status) {
        errno = EINVAL;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    if(mutex_lock(&service->mutex) < 0)
        return -1;

    status->state = service->state;
    status->poll_interval_ms = service->poll_interval_ms;
    status->registered_streams = service->registered_streams;
    status->active_stream = service->active_stream;
    status->last_error_stream = service->last_error_stream;
    status->last_error = service->last_error;
    status->sweeps = service->sweeps;
    status->polls = service->polls;
    status->successful_polls = service->successful_polls;
    status->underruns = service->underruns;
    status->hard_errors = service->hard_errors;

    mutex_unlock(&service->mutex);
    return 0;
}

int snd_stream_service_destroy(snd_stream_service_t *service,
                               uint32_t timeout_ms) {
    uint64_t deadline;
    if(require_thread_context() < 0)
        return -1;
    if(!service || !timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(service->thread == thd_get_current()) {
        errno = EDEADLK;
        return -1;
    }

    deadline = timer_ms_gettime64() + timeout_ms;
    if(mutex_lock(&service->mutex) < 0)
        return -1;
    if(service->state == SND_STREAM_SERVICE_RUNNING)
        service->state = SND_STREAM_SERVICE_STOPPING;
    sem_signal(&service->work);

    while(service->state != SND_STREAM_SERVICE_STOPPED) {
        uint32_t remaining = deadline_remaining(deadline);

        if(!remaining) {
            mutex_unlock(&service->mutex);
            errno = ETIMEDOUT;
            return -1;
        }
        if(cond_wait_timed(&service->idle, &service->mutex,
                           (int)remaining) < 0) {
            mutex_unlock(&service->mutex);
            return -1;
        }
    }
    mutex_unlock(&service->mutex);

    if(thd_join(service->thread, NULL) < 0)
        return -1;

    sem_destroy(&service->work);
    cond_destroy(&service->idle);
    (void)mutex_destroy(&service->mutex);
    free(service);
    return 0;
}
