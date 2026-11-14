#include <kos.h>
#include <dc/sound/sound.h>
#include <errno.h>
#include <stdio.h>
#include "owner.h"
#include "score.h"
static asq_owner_t resources;
#define REQUIRE(x) do { if(!(x)) { printf("ASQ RESOURCE FAIL line=%d errno=%d client=%lu result=%ld slot0=%lu slot1=%lu\n",__LINE__,errno,(unsigned long)resources.client.state,(long)resources.client.receipt.result,(unsigned long)resources.slots[0].state,(unsigned long)resources.slots[1].state);goto hold; } } while(0)
static int await_state(unsigned slot,unsigned state) {
    uint64_t deadline=timer_ms_gettime64()+5000;
    while(resources.slots[slot].state!=state) {
        if(asq_owner_pump(&resources)<0 || timer_ms_gettime64()>=deadline) return -1;
        thd_pass();
    }
    return 0;
}
int main(void) {
    uint32_t available;
    REQUIRE(snd_init()==0);
    REQUIRE(asq_owner_init(&resources,64,1000)==0);
    available=snd_mem_available();
    for(unsigned generation=1;generation<=2;generation++) {
        for(unsigned slot=0;slot<2;slot++) {
            REQUIRE(asq_owner_begin(&resources,slot,score,sizeof(score))==0);
            REQUIRE(await_state(slot,ASQH_READY)==0);
            REQUIRE(resources.slots[slot].generation==generation && resources.slots[slot].admitted);
            REQUIRE(asq_owner_query(&resources,slot)==0 && await_state(slot,ASQH_READY)==0);
            REQUIRE(resources.client.receipt.programs==2 && resources.client.receipt.event_count==9);
            REQUIRE(resources.client.receipt.borrowed==0 && resources.client.receipt.playing==0);
        }
        for(unsigned slot=0;slot<2;slot++) {
            REQUIRE(asq_owner_close(&resources,slot)==0 && await_state(slot,ASQH_CLOSED)==0);
            REQUIRE(resources.slots[slot].source==0 && resources.slots[slot].events==0);
        }
        REQUIRE(snd_mem_available()==available);
    }
    /* Cancellation before publication still retires the native transfer. */
    REQUIRE(asq_owner_begin(&resources,0,score,sizeof(score))==0);
    REQUIRE(asq_owner_close(&resources,0)==0 && await_state(0,ASQH_CLOSED)==0);
    REQUIRE(resources.slots[0].generation==2 && snd_mem_available()==available);
    printf("ASQ RESOURCE PASS: two programs; two slots; generations1..2; cancelled upload retired; allocations restored; no playback or physical sample-retirement claim\n");
hold:
    for(;;) thd_sleep(1000);
    return 0;
}
