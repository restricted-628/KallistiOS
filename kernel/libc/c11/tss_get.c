/* KallistiOS ##version##

   tss_get.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

void *tss_get(tss_t key) {
    return kthread_getspecific(key);
}
