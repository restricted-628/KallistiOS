/* KallistiOS ##version##

   thrd_sleep.c
   Copyright (C) 2014 Lawrence Sebald

*/

#include <threads.h>
#include <errno.h>

int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    int ms;

    /* Make sure we aren't inside an interrupt first... */
    if(irq_inside_int()) {
        if(remaining)
            *remaining = *duration;

        return -1;
    }

    /* Calculate the number of milliseconds to sleep for. No, you don't get
       anywhere near nanosecond precision here. */
    ms = duration->tv_sec * 1000 + duration->tv_nsec / 1000000;

    /* We need to sleep for *at least* how long is specified, so if they've
       given us a non-whole number of milliseconds, then add one to the time. */
    if(duration->tv_nsec % 1000000)
        ++ms;

    /* Make sure they gave us something valid. */
    if(ms < 0) {
        if(remaining)
            *remaining = *duration;

        return -1;
    }

    /* Sleep! */
    thd_sleep(ms);

    /* thd_sleep will always sleep for at least the specified time, so clear out
       the remaining time, if it was given to us. */
    if(remaining) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }

    return 0;
}
