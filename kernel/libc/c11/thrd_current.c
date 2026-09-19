/* KallistiOS ##version##

   thrd_current.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

thrd_t thrd_current(void) {
    return thd_get_current();
}
