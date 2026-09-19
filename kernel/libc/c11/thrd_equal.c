/* KallistiOS ##version##

   thrd_equal.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

int thrd_equal(thrd_t thr0, thrd_t thr1) {
    return thr0 == thr1;
}
