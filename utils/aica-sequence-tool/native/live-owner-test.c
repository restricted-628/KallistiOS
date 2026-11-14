#define main core_test_main
#include "live-test.c"
#undef main
#include "live-owner.h"
#include <dc/sound/aica_sequence_resource.h>
#include <dc/sound/aica_observation.h>
#include <errno.h>
static asql_owner_t host;
static aica_music_owner_t host_music;
static asq_owner_t host_resources;
static unsigned interrupt_context;
static uint32_t features=AICA_DRIVER_FEATURE_SEQUENCE_CONTROL;
int irq_inside_int(void) { return (int)interrupt_context; }
uint64_t timer_ms_gettime64(void) { return 1; }
uint32_t snd_iface_observation_features(void) { return features; }
int snd_observation_session(uint32_t *s) { *s=active;return 0; }
int snd_iface_observation_read(uint32_t offset,void *out,uint32_t bytes) {
    if(offset==AICA_MEM_OBSERVATION_HEADER) {
        aica_observation_header_t h={0};C(bytes==sizeof(h));h.ready=1;
        h.layout_version=AICA_OBSERVATION_LAYOUT_VERSION;h.active_session=active;memcpy(out,&h,bytes);return 0;
    }
    C(offset==AICA_MEM_SEQUENCE_CONTROL_RECEIPT && (bytes==4 || bytes==256));
    return client_read(NULL,out,bytes/4);
}
int snd_iface_observation_submit(void *p,uint32_t words,uint64_t deadline) {
    C(words==16 && deadline>1);
    if(transport==1) { errno=EIO;return -1; }
    C(asql_command(&live,p,active));
    if(transport==2) { errno=EIO;return -1; }return 0;
}
static int host_command(unsigned op,const uint32_t *args) {
    C(asql_owner_command(&host,op,0,1,args)>=0);unsigned n=0;
    while(asql_owner_poll(&host)==0) { step(0);C(++n<64); }
    C(host.client.state==ASQC_DONE);int rc=host.client.receipt.result;C(asql_owner_consume(&host)==0);return rc;
}
int main(int argc,char **argv) {
    C(argc==2);unsigned mode=(unsigned)atoi(argv[1]);init();active=1;
    host_music.initialized=1;host_music.session=1;host_music.client.state=AICA_MC_IDLE;
    host_music.retained=1;host_music.count=64;
    host_resources.initialized=1;host_resources.session=1;host_resources.client.state=ASQC_IDLE;host_resources.active=8;
    host_resources.slots[0].state=ASQH_READY;host_resources.slots[0].generation=1;host_resources.slots[0].admitted=1;
    const uint32_t zero[4]={0},play[4]={0,1,0,10};
    if(mode==1) features=0;
    if(mode==2) host_resources.session=2;
    if(mode==3) interrupt_context=1;
    if(mode==4) host_music.client.state=AICA_MC_WAIT;
    if(mode>=1 && mode<=4) { C(asql_owner_init(&host,&host_music,&host_resources,100)<0 && !host.initialized);goto done; }
    C(asql_owner_init(&host,&host_music,&host_resources,100)==0);
    if(mode==5) {
        host_resources.active=0;C(asql_owner_command(&host,ASQL_BIND,0,1,zero)<0 && errno==EBUSY);
        host_resources.active=8;host_music.client.state=AICA_MC_WAIT;
        C(asql_owner_command(&host,ASQL_BIND,0,1,zero)<0 && errno==EBUSY);goto done;
    }
    if(mode==7 || mode==8) transport=mode-6;
    C(asql_owner_command(&host,ASQL_BIND,0,1,zero)>=0);
    if(mode==6) {
        step(0);active=2;C(asql_owner_poll(&host)<0 && host.client.state==ASQC_LOST);
        C(mw.protected_ports==1 && host_music.retained && host_music.count==64);goto done;
    }
    if(mode==7 || mode==8) {
        uint32_t old[16];memcpy(old,host.client.packet,64);transport=0;
        C(asql_owner_retry(&host)==1 && !memcmp(old,host.client.packet,64));
    }
    step(0);C(asql_owner_poll(&host)==1 && asql_owner_consume(&host)==0);
    C(host_command(ASQL_PLAY,play)==0);step(0);for(unsigned i=0;i<5;i++) step(0);
    C(host_command(ASQL_PAUSE,zero)==0);C(asql_owner_command(&host,ASQL_CONTINUE,0,1,zero)==1);
    C(asql_owner_poll(&host)==0 && asql_owner_cancel(&host,0)==1);step(0);
    C(asql_owner_poll(&host)==1 && host.client.receipt.cancelled_result==ASQL_CANCELLED);
    C(asql_owner_consume(&host)==0 && !catalog.slots[0].playing);
    C(asq_catalog_drop(&catalog,0,1)==0 && records[0].voice_refs && host_music.retained);
    C(host_command(ASQL_UNBIND,zero)==0 && !mw.protected_ports);
done:
    printf("ASQ live owner case%u: %u checks PASS\n",mode,checks);return 0;
}
