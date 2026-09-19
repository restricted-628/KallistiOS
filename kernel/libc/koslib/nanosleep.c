/* KallistiOS ##version##

   nanosleep.c
   Copyright (C) 2014 Lawrence Sebald

*/

#include <time.h>
#include <errno.h>
#include <kos/thread.h>

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
    int ms;

    /* Make sure we aren't inside an interrupt first... */
    if(irq_inside_int()) {
        if(rmtp)
            *rmtp = *rqtp;

        errno = EINTR;  /* XXXX: Sorta. */
        return -1;
    }

    /* Calculate the number of milliseconds to sleep for. No, you don't get
       anywhere near nanosecond precision here. */
    ms = rqtp->tv_sec * 1000 + rqtp->tv_nsec / 1000000;

    /* We need to sleep for *at least* how long is specified, so if they've
       given us a non-whole number of milliseconds, then add one to the time. */
    if(rqtp->tv_nsec % 1000000)
        ++ms;

    /* Make sure they gave us something valid. */
    if(ms < 0) {
        if(rmtp)
            *rmtp = *rqtp;

        errno = EINVAL;
        return -1;
    }

    /* Sleep! */
    thd_sleep(ms);

    /* thd_sleep will always sleep for at least the specified time, so clear out
       the remaining time, if it was given to us. */
    if(rmtp) {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }

    return 0;
}
