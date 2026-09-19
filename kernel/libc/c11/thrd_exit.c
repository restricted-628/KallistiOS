/* KallistiOS ##version##

   thrd_exit.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

_Noreturn void thrd_exit(int res) {
    thd_exit((void *)res);
}
