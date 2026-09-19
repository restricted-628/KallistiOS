/* KallistiOS ##version##

   thrd_detach.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

int thrd_detach(thrd_t thr) {
    if(thd_detach(thr))
        return thrd_error;

    return thrd_success;
}
