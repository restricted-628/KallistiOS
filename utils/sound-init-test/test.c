#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __ARCH_TYPES_H
#define uint32 uint32_t
#define uint8 uint8_t
#include "../../kernel/arch/dreamcast/include/dc/sound/aica_comm.h"
#include "../../kernel/arch/dreamcast/sound/arm/aica_cmd_iface.h"
#undef uint32
#undef uint8
#define SPU_RAM_UNCACHED_BASE 0xa0800000u
#define DBG_DEBUG 0
#define DBG_ERROR 1
#define SND_DRIVER_FEATURE_SYNC_CHANNELS 1u
#define g2_lock_scoped() ((void)0)
#define dbglog(...) ((void)0)
typedef struct { uint32_t features; } snd_driver_status_t;
static unsigned checks,query_count,shutdown_count,disable_count,dsp_count,passes;
static uint64_t now,command_ready,response_ready;
static unsigned malformed,query_error,query_cost,missing_feature,alloc_error;
static unsigned advance_on_response;
static uint32_t query_budget;
static int initted;
static uint32_t driver_features;
static unsigned char snd_stream_drv_data[4];
static size_t snd_stream_drv_size=3;
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort(); } } while(0)
static uint64_t timer_ms_gettime64(void) { return now; }
static void thd_sleep(unsigned n) { now+=n; }
static void thd_pass(void) { ++now;++passes;C(passes<=100); }
static uint32_t g2_read_32_raw(uint32_t address) {
    uint32_t offset=address-SPU_RAM_UNCACHED_BASE;
    bool response=offset>=AICA_MEM_RESP_QUEUE;
    uint32_t base=response?AICA_MEM_RESP_QUEUE:AICA_MEM_CMD_QUEUE;
    uint32_t end=response?AICA_MEM_CHANNELS:AICA_MEM_RESP_QUEUE;
    uint32_t field=offset-base;
    if(response && advance_on_response) { now+=advance_on_response;advance_on_response=0; }
    if(field==offsetof(aica_queue_t,valid)) return now>=(response?response_ready:command_ready);
    if(field==offsetof(aica_queue_t,data)) return base+sizeof(aica_queue_t);
    if(field==offsetof(aica_queue_t,size)) return malformed==(response?2u:1u)?3u:end-base-sizeof(aica_queue_t);
    if(field==offsetof(aica_queue_t,head) || field==offsetof(aica_queue_t,tail)) return 0;
    if(field==offsetof(aica_queue_t,process_ok)) return 1;
    C(0);return 0;
}
static void snd_observation_load_begin(void) {}
static void spu_disable(void) { ++disable_count; }
static void spu_enable(void) {}
static void spu_memset_sq(unsigned a,unsigned b,unsigned n) { C(a==0 && b==0 && n==AICA_RAM_START); }
static void spu_memload_sq(unsigned a,void *p,size_t n) { C(a==0 && p==snd_stream_drv_data && n==4); }
static int snd_mem_init(unsigned start) { C(start==AICA_RAM_START);if(alloc_error) { errno=ENOMEM;return -1; }return 0; }
static int snd_driver_get_status(snd_driver_status_t *s,uint32_t timeout) {
    ++query_count;query_budget=timeout;C(timeout>0 && timeout<=100);
    C(now>=command_ready && now>=response_ready);now+=query_cost;
    if(query_error) { errno=(int)query_error;return -1; }
    s->features=missing_feature?0:SND_DRIVER_FEATURE_SYNC_CHANNELS;return 0;
}
static void snd_shutdown(void) { ++shutdown_count;initted=0;spu_disable();errno=EIO; }
static void snd_dsp_system_reset(void) { ++dsp_count; }
#include "startup.inc"
static void reset(void) {
    now=command_ready=response_ready=0;
    query_count=shutdown_count=disable_count=dsp_count=passes=0;
    malformed=query_error=query_cost=missing_feature=alloc_error=advance_on_response=0;
    query_budget=0;initted=0;driver_features=99;errno=0;
}
int main(void) {
    reset();C(snd_init()==0);C(query_count==1 && query_budget==100 && dsp_count==1);
    C(snd_init()==0 && query_count==1 && now==10);
    for(unsigned which=0;which<2;which++) {
        reset();if(which) response_ready=75;else command_ready=75;
        C(snd_init()==0);C(query_count==1 && query_budget==35 && now==75 && passes==65);
        reset();if(which) response_ready=UINT64_MAX;else command_ready=UINT64_MAX;
        C(snd_init()<0 && errno==ETIMEDOUT);C(now==110 && query_count==0 && shutdown_count==1 && !initted);
        reset();malformed=which+1;C(snd_init()<0 && errno==EPROTO);
        C(query_count==0 && passes==0 && shutdown_count==1);
    }
    reset();command_ready=40;response_ready=70;query_cost=7;
    C(snd_init()==0 && now==77 && query_budget==40 && query_count==1);
    reset();advance_on_response=100;C(snd_init()<0 && errno==ETIMEDOUT);
    C(query_count==0 && shutdown_count==1);
    reset();query_error=ENODEV;C(snd_init()<0 && errno==ENODEV);
    C(query_count==1 && passes==0 && shutdown_count==1 && !initted);
    reset();query_error=ETIMEDOUT;query_cost=100;C(snd_init()<0 && errno==ETIMEDOUT);
    C(query_count==1 && now==110 && shutdown_count==1);
    reset();missing_feature=1;C(snd_init()<0 && errno==EPROTO);
    C(query_count==1 && shutdown_count==1 && dsp_count==0);
    reset();alloc_error=1;C(snd_init()<0 && errno==ENOMEM);
    C(query_count==0 && shutdown_count==0 && disable_count==2 && !initted);
    printf("Sound startup: %u checks PASS\n",checks);return 0;
}
