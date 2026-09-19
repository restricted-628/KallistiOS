/* KallistiOS ##version##

   sched_yield.c
   Copyright (C) 2020 Lawrence Sebald
*/

#include <kos/thread.h>

int sched_yield(void) {
    thd_pass();
    return 0;
}
