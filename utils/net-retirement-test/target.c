/* Scheduler evidence only: the guest intentionally remains resident after PASS. */
#include <kos.h>
#include <errno.h>
#include <stdio.h>
#include "../../kernel/net/net_shutdown_internal.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
static volatile unsigned progress;
static void *ticker(void *unused) {
    (void)unused;
    for(;;) { ++progress; thd_sleep(20); }
    return NULL;
}
static void *refuse(void *unused) {
    (void)unused;
    arch_net_shutdown_wait(EBUSY, NET_SHUTDOWN_NETWORK);
}
int main(void) {
    if(!thd_create(0,ticker,NULL) || !thd_create(0,refuse,NULL)) {
        puts("NETRET RESULT FAIL creation");
    }
    else {
        thd_sleep(1300);
        printf("\nNETRET RESULT %s error=%d stage=%d progress=%u\n",
               arch_net_shutdown_error==EBUSY && arch_net_shutdown_stage==NET_SHUTDOWN_NETWORK
               && progress>10 ? "PASS" : "FAIL",
               arch_net_shutdown_error,arch_net_shutdown_stage,progress);
    }
    fflush(stdout);
    for(;;) thd_sleep(1000);
}
