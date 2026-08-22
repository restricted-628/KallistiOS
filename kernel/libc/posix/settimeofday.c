/* KallistiOS ##version##

   settimeofday.c
   Copyright (C) 2024 Falco Girgis
   Copyright (C) 2026 Joseph Black
*/

#include <time.h>
#include <sys/time.h>
#include <errno.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    struct timespec tspec;
    (void)tz;

    if(!tv) {
        errno = EFAULT;
        return -1;
    }

    if(tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
        errno = EINVAL;
        return -1;
    }

    tspec.tv_sec  = tv->tv_sec;
    tspec.tv_nsec = tv->tv_usec * 1000;

    return clock_settime(CLOCK_REALTIME, &tspec);
}
