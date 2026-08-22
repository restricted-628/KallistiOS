/* KallistiOS ##version##

   timer_event.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <kos/timer_event.h>
#include <kos/workqueue.h>

struct timer_event {
    workqueue_job_t job;
    mutex_t operation_lock;
    mutex_t state_lock;
    timer_event_callback_t callback;
    void *callback_data;
    workqueue_t *queue;
    uint64_t deadline_ms;
    uint64_t interval_ms;
    uint64_t rearm_deadline_ms;
    uint64_t rearm_interval_ms;
    uint64_t expirations;
    uint64_t overruns;
    bool attached;
    bool armed;
    bool running;
    bool cancelling;
    bool cancel_requested;
    bool rearm_requested;
    bool destroying;
};

static mutex_t timer_service_lock = MUTEX_INITIALIZER;
static workqueue_t *timer_service_queue;
static unsigned int timer_service_users;

static timer_event_t *job_to_timer(workqueue_job_t *job) {
    return (timer_event_t *)((char *)job - offsetof(timer_event_t, job));
}

static bool callback_is_current(timer_event_t *timer) {
    bool current;

    mutex_lock(&timer->state_lock);
    current = timer->running && timer->queue &&
              thd_get_current() == workqueue_get_thread(timer->queue);
    mutex_unlock(&timer->state_lock);
    return current;
}

static int timer_service_attach(timer_event_t *timer) {
    workqueue_t *queue;

    mutex_lock(&timer_service_lock);
    if(timer->attached) {
        mutex_unlock(&timer_service_lock);
        return 0;
    }

    /* The first armed timer pays for the carrier. Subsequent timers attach to
       the same serialized dispatcher without allocating another stack. */
    queue = timer_service_queue;
    if(!queue) {
        queue = workqueue_create();
        if(!queue) {
            mutex_unlock(&timer_service_lock);
            errno = ENOMEM;
            return -1;
        }

        thd_set_label(workqueue_get_thread(queue), "[timer-events]");
        timer_service_queue = queue;
    }

    timer->queue = queue;
    timer->attached = true;
    ++timer_service_users;
    mutex_unlock(&timer_service_lock);
    return 0;
}

static void timer_service_detach(timer_event_t *timer) {
    workqueue_t *queue = NULL;

    mutex_lock(&timer_service_lock);
    if(timer->attached) {
        timer->attached = false;
        timer->queue = NULL;
        /* An object remains attached across cancel/rearm, avoiding thread
           churn. Destroying the final attached object releases the carrier. */
        if(--timer_service_users == 0) {
            queue = timer_service_queue;
            timer_service_queue = NULL;
        }
    }
    mutex_unlock(&timer_service_lock);

    if(queue)
        workqueue_destroy(queue);
}

static bool periodic_deadline(uint64_t previous, uint64_t interval,
                              uint64_t now, uint64_t *next,
                              uint64_t *overruns) {
    uint64_t skipped = 0;
    uint64_t deadline;

    if(previous > UINT64_MAX - interval)
        return false;

    /* Advance from the prior deadline rather than callback completion so
       ordinary dispatch jitter does not accumulate as timer drift. Coalesce
       periods which elapsed while the shared callback thread was delayed. */
    deadline = previous + interval;
    if(deadline <= now) {
        skipped = (now - deadline) / interval + 1;
        if(skipped > (UINT64_MAX - deadline) / interval)
            return false;
        deadline += skipped * interval;
    }

    *next = deadline;
    *overruns = skipped;
    return true;
}

static void timer_event_dispatch(workqueue_t *queue, workqueue_job_t *job) {
    timer_event_t *timer = job_to_timer(job);
    timer_event_callback_t callback;
    uint64_t interval;
    uint64_t deadline;
    uint64_t next = 0;
    uint64_t skipped = 0;
    bool invoke;
    bool repeat = false;
    bool schedule = false;

    (void)queue;

    mutex_lock(&timer->state_lock);
    timer->armed = false;
    timer->running = true;
    invoke = !timer->cancel_requested && !timer->destroying;
    callback = timer->callback;
    interval = timer->interval_ms;
    deadline = timer->deadline_ms;
    if(invoke)
        ++timer->expirations;
    mutex_unlock(&timer->state_lock);

    /* No timer lock is held across application code. The operation result is
       reconciled afterward with self-rearm, self-cancel, or an external
       cancellation barrier. */
    if(invoke)
        repeat = callback(timer, timer->callback_data);

    mutex_lock(&timer->state_lock);
    timer->running = false;

    if(!timer->cancel_requested && !timer->cancelling && !timer->destroying) {
        if(timer->rearm_requested) {
            next = timer->rearm_deadline_ms;
            timer->interval_ms = timer->rearm_interval_ms;
            schedule = true;
        }
        else if(interval && repeat &&
                periodic_deadline(deadline, interval,
                                  timer_ms_gettime64(), &next, &skipped)) {
            timer->interval_ms = interval;
            timer->overruns += skipped;
            schedule = true;
        }
    }

    timer->rearm_requested = false;
    timer->cancel_requested = false;
    timer->deadline_ms = schedule ? next : 0;
    timer->armed = schedule;
    job->time_ms = next;
    mutex_unlock(&timer->state_lock);

    /* workqueue_cancel_ex() publishes its cancellation barrier before waiting
       for this callback. A requeue racing external cancel is rejected with
       ECANCELED, which makes cancellation a drain rather than a timing race. */
    if(schedule && workqueue_enqueue_ex(timer->queue, job) < 0) {
        mutex_lock(&timer->state_lock);
        timer->armed = false;
        timer->deadline_ms = 0;
        mutex_unlock(&timer->state_lock);
    }
}

static int timer_event_cancel_locked(timer_event_t *timer) {
    workqueue_job_info_t job_info;
    bool was_active;
    int result;
    int saved_errno;

    if(!timer->attached) {
        errno = ENOENT;
        return -1;
    }

    /* Publish cancellation before querying the queue. This covers the narrow
       interval where dispatch has removed the job but has not called the user
       callback yet. */
    mutex_lock(&timer->state_lock);
    was_active = timer->armed || timer->running;
    timer->cancelling = true;
    timer->cancel_requested = true;
    timer->rearm_requested = false;
    mutex_unlock(&timer->state_lock);

    if(workqueue_job_get_info(timer->queue, &timer->job, &job_info) == 0 &&
       (job_info.queued || job_info.running)) {
        result = workqueue_cancel_ex(timer->queue, &timer->job);
        saved_errno = errno;
    }
    else {
        result = -1;
        saved_errno = ENOENT;
    }

    mutex_lock(&timer->state_lock);
    timer->armed = false;
    timer->running = false;
    timer->cancelling = false;
    timer->cancel_requested = false;
    timer->deadline_ms = 0;
    mutex_unlock(&timer->state_lock);

    if(result < 0 && !was_active) {
        errno = saved_errno;
        return -1;
    }

    return 0;
}

timer_event_t *timer_event_create(timer_event_callback_t callback, void *data) {
    timer_event_t *timer;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }
    if(!callback) {
        errno = EINVAL;
        return NULL;
    }

    timer = calloc(1, sizeof(*timer));
    if(!timer)
        return NULL;

    if(mutex_init(&timer->operation_lock, MUTEX_TYPE_NORMAL) < 0) {
        free(timer);
        return NULL;
    }
    if(mutex_init(&timer->state_lock, MUTEX_TYPE_NORMAL) < 0) {
        mutex_destroy(&timer->operation_lock);
        free(timer);
        return NULL;
    }

    timer->callback = callback;
    timer->callback_data = data;
    timer->job.cb = timer_event_dispatch;
    return timer;
}

int timer_event_destroy(timer_event_t *timer) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!timer) {
        errno = EINVAL;
        return -1;
    }
    if(callback_is_current(timer)) {
        errno = EDEADLK;
        return -1;
    }

    mutex_lock(&timer->operation_lock);
    mutex_lock(&timer->state_lock);
    timer->destroying = true;
    mutex_unlock(&timer->state_lock);
    (void)timer_event_cancel_locked(timer);
    timer_service_detach(timer);
    mutex_unlock(&timer->operation_lock);

    mutex_destroy(&timer->state_lock);
    mutex_destroy(&timer->operation_lock);
    free(timer);
    return 0;
}

int timer_event_arm(timer_event_t *timer, uint64_t delay_ms,
                    uint64_t interval_ms) {
    uint64_t now;
    uint64_t deadline;
    int result;
    int saved_errno;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!timer) {
        errno = EINVAL;
        return -1;
    }

    now = timer_ms_gettime64();
    if(delay_ms > UINT64_MAX - now) {
        errno = EOVERFLOW;
        return -1;
    }
    deadline = now + delay_ms;
    if(interval_ms && deadline > UINT64_MAX - interval_ms) {
        errno = EOVERFLOW;
        return -1;
    }

    mutex_lock(&timer->state_lock);
    if(timer->running && timer->queue &&
       thd_get_current() == workqueue_get_thread(timer->queue)) {
        if(timer->cancelling || timer->destroying) {
            mutex_unlock(&timer->state_lock);
            errno = ECANCELED;
            return -1;
        }

        timer->cancel_requested = false;
        timer->rearm_requested = true;
        timer->rearm_deadline_ms = deadline;
        timer->rearm_interval_ms = interval_ms;
        mutex_unlock(&timer->state_lock);
        return 0;
    }
    if(timer->destroying) {
        mutex_unlock(&timer->state_lock);
        errno = ECANCELED;
        return -1;
    }
    mutex_unlock(&timer->state_lock);

    mutex_lock(&timer->operation_lock);
    if(timer_service_attach(timer) < 0) {
        mutex_unlock(&timer->operation_lock);
        return -1;
    }

    (void)timer_event_cancel_locked(timer);

    mutex_lock(&timer->state_lock);
    timer->cancel_requested = false;
    timer->rearm_requested = false;
    timer->deadline_ms = deadline;
    timer->interval_ms = interval_ms;
    timer->armed = true;
    timer->job.time_ms = deadline;
    mutex_unlock(&timer->state_lock);

    result = workqueue_enqueue_ex(timer->queue, &timer->job);
    saved_errno = errno;
    if(result < 0) {
        mutex_lock(&timer->state_lock);
        timer->armed = false;
        timer->deadline_ms = 0;
        mutex_unlock(&timer->state_lock);
    }
    mutex_unlock(&timer->operation_lock);

    if(result < 0)
        errno = saved_errno;
    return result;
}

int timer_event_cancel(timer_event_t *timer) {
    int result;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!timer) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&timer->state_lock);
    if(timer->running && timer->queue &&
       thd_get_current() == workqueue_get_thread(timer->queue)) {
        timer->cancel_requested = true;
        timer->rearm_requested = false;
        mutex_unlock(&timer->state_lock);
        return 0;
    }
    mutex_unlock(&timer->state_lock);

    mutex_lock(&timer->operation_lock);
    result = timer_event_cancel_locked(timer);
    mutex_unlock(&timer->operation_lock);
    return result;
}

int timer_event_get_info(timer_event_t *timer, timer_event_info_t *info) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!timer || !info) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&timer->state_lock);
    info->armed = timer->armed;
    info->running = timer->running;
    info->cancelling = timer->cancelling;
    info->deadline_ms = timer->deadline_ms;
    info->interval_ms = timer->interval_ms;
    info->expirations = timer->expirations;
    info->overruns = timer->overruns;
    mutex_unlock(&timer->state_lock);
    return 0;
}
