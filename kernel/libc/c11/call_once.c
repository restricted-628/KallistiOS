/* KallistiOS ##version##

   call_once.c
   Copyright (C) 2014 Lawrence Sebald
*/

#include <threads.h>

void call_once(once_flag *flag, void (*func)(void)) {
    (void)kthread_once(flag, func);
}
