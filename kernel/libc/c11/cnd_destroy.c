/* KallistiOS ##version##

   cnd_destroy.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

void cnd_destroy(cnd_t *cond) {
    (void)cond_destroy(cond);
}
