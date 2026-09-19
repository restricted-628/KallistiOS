/* KallistiOS ##version##

   mtx_destroy.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

void mtx_destroy(mtx_t *mtx) {
    (void)mutex_destroy(mtx);
}
