/* Real-library custom init-flag pointer pairing; no NIC hardware claim. */
#include <kos.h>
#include <errno.h>
#include <stdio.h>
#include "../../kernel/net/net_shutdown_internal.h"
#ifndef TEST_MODE
#define TEST_MODE 0
#endif
#if TEST_MODE == 1
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NET);
#else
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif
extern void (*net_shutdown_weak)(void), (*eth_shutdown_weak)(void);
static unsigned custom_calls;
static int custom_result;
static void custom_net(void) { }
static void custom_eth(void) { }
#if TEST_MODE == 4
static int custom_check(void) { ++custom_calls; errno=EACCES; return custom_result; }
const struct net_shutdown_binding arch_net_shutdown_custom={custom_net,custom_check};
const struct net_shutdown_binding arch_eth_shutdown_custom={custom_eth,custom_check};
#endif
int main(void) {
    int result, error, ok;
    if(TEST_MODE == 2)
        (void)net_init(0); /* Invocation/link coverage; device absence may refuse. */
    if(TEST_MODE >= 3) {
        net_shutdown_weak=custom_net;
        eth_shutdown_weak=custom_eth;
    }
    custom_result=-1;
    result=arch_net_shutdown_preflight();
    error=errno;
    if(TEST_MODE == 3)
        ok=result==-1&&error==ENOSYS&&custom_calls==0;
    else if(TEST_MODE == 4) {
        ok=result==-1&&error==EACCES&&custom_calls==1;
        custom_result=0;
        ok=ok&&arch_net_shutdown_preflight()==0&&custom_calls==3;
    }
    else
        ok=result==0;
    printf("\nNETRET POINTER %s mode=%d result=%d error=%d calls=%u\n",
           ok?"PASS":"FAIL",TEST_MODE,result,error,custom_calls);
    fflush(stdout);
    for(;;) thd_sleep(1000);
}
