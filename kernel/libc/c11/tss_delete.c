/* KallistiOS ##version##

   tss_delete.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

void tss_delete(tss_t key) {
    (void)kthread_key_delete(key);
}
