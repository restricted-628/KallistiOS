#include "music_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,claimed,refs,starts,stops,transport,read_error,read_calls,torn,session_error;
static uint32_t active_session;
static uint64_t physical;
static aica_music_t music;
static aica_music_wire_t wire;
static aica_music_receipt_t receipt;
static aica_music_client_t client;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static int resolve(void *ctx,const aica_music_event_t *e,unsigned bank,unsigned program,unsigned drum,aica_music_tone_t *tone) {
    (void)ctx;(void)bank;(void)program;(void)drum;
    *tone=(aica_music_tone_t){.bank=e->bank,.generation=e->bank_generation,.root_key=60,
        .config={.base=0x31000,.length=16,.loopend=16,.freq=22050,.vol=255,.pan=128,.direct_level=15}};return 0;
}
static int retain(void *ctx,unsigned voice,const aica_music_tone_t *t) {
    (void)ctx;(void)t;physical|=(uint64_t)1<<voice;return 0;
}
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;(void)c;CHECK(token && ((physical>>voice)&1));++starts;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) { (void)ctx;(void)c;CHECK((physical>>voice)&1); }
static void stop(void *ctx,unsigned voice,int release) { (void)ctx;(void)release;CHECK((physical>>voice)&1);++stops; }
static int claim(void *ctx,uint32_t low,uint32_t high) { (void)ctx;CHECK(low || high);++claimed;return 1; }
static int pin(void *ctx,uint32_t session,uint32_t bank,uint32_t generation) { (void)ctx;CHECK(session==1 && bank<128 && generation);++refs;return 1; }
static int unpin(void *ctx,uint32_t session,uint32_t bank,uint32_t generation) { (void)ctx;CHECK(session==1 && bank<128 && generation && refs);--refs;return 1; }
static int read_words(void *ctx,unsigned offset,uint32_t *out,unsigned count) {
    (void)ctx;++read_calls;CHECK(offset+count<=64);
    if(read_error && read_calls==read_error) return -1;
    memcpy(out,(const uint8_t *)&receipt+offset*4,count*4);
    if(torn && count==64) { receipt.sequence+=2;torn=0; }
    return 0;
}
static int session(void *ctx,uint32_t *out) { (void)ctx;if(session_error) return -1;*out=active_session;return 0; }
static int submit(void *ctx,const uint32_t words[16]) {
    aica_cmd_t *p;(void)ctx;
    if(!transport) return 0;
    if(transport==3) return -1;
    p=calloc(1,64);CHECK(p!=NULL);p->size=words[0];p->cmd=words[1];p->timestamp=words[2];p->cmd_id=words[3];
    memcpy(p->misc,words+4,16);memcpy(p->cmd_data,words+8,32);
    CHECK(aica_music_wire_command(&wire,p,active_session)==1);free(p);
    return transport==2?-1:1;
}
static void initialize(void) {
    aica_music_ops_t m={.resolve=resolve,.retain=retain,.start=start,.update=update,.stop=stop};
    aica_music_wire_ops_t w={claim,pin,unpin,NULL};
    aica_music_client_ops_t c={read_words,session,submit,NULL};
    claimed=refs=starts=stops=read_calls=read_error=torn=session_error=0;physical=0;transport=1;active_session=1;
    CHECK(aica_music_init(&music,4,&m)==0);CHECK(aica_music_wire_init(&wire,&music,&receipt,&w));
    CHECK(aica_music_client_init(&client,1,&c)==0);
    CHECK(aica_music_client_poll(&client)==1 && client.state==AICA_MC_IDLE);
}
static void command(unsigned op,unsigned p,uint32_t g,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    uint32_t args[4]={a,b,c,d};
    CHECK(aica_music_client_begin(&client,op,p,g,args)==1);
    CHECK(aica_music_client_poll(&client)==1 && client.receipt.result==0);
    CHECK(aica_music_client_consume(&client)==0);
}
static void open_port(void) { command(1,0,0,15,0,0,0);command(2,0,0,3,1,0,0);CHECK(refs==1 && claimed==1 && client.generation[0]==1); }
static void ordering(void) {
    uint32_t volume[4]={AICA_MUSIC_VOLUME,(uint32_t)-64,0,0};
    initialize();open_port();command(4,0,1,0x90,60,100,4);
    CHECK(starts==0 && music.count==1);
    CHECK(aica_music_client_begin(&client,8,0,1,volume)==1);
    CHECK(aica_music_client_poll(&client)==0 && client.receipt.result==1);
    CHECK(aica_music_client_retry(&client)==1);
    CHECK(aica_music_service(&music,0)==1 && starts==1);
    aica_music_wire_service(&wire);
    CHECK(aica_music_client_poll(&client)==1 && client.receipt.volume==-64);
    CHECK(aica_music_client_begin(&client,8,0,1,volume)<0);
    CHECK(aica_music_client_consume(&client)==0);
    command(5,0,1,65535,0,0,0);CHECK(stops==1 && physical);
    command(6,0,1,1,0,0,0);command(7,0,1,0,0,0,0);command(9,0,1,0,0,0,0);
    command(3,0,1,0,0,0,0);CHECK(refs==0 && physical);
    command(2,0,0,3,2,0,0);CHECK(client.generation[0]==2);
    command(13,0,0,0,0,0,0);CHECK(refs==0 && physical);
    command(2,0,0,3,3,0,0);command(12,0,0,0,0,0,0);
    CHECK(aica_music_client_quiesce(&client)==1);
    CHECK(aica_music_client_poll(&client)==1 && !refs && physical && client.admission_closed);
    CHECK(aica_music_client_consume(&client)==0);
    CHECK(aica_music_client_begin(&client,8,0,3,volume)<0);
    command(10,0,3,0,0,0,0);
}
static void uncertainty(void) {
    uint32_t args[4]={15,0,0,0},cookie;
    initialize();transport=0;
    CHECK(aica_music_client_begin(&client,1,0,0,args)==0 && client.state==AICA_MC_WAIT);
    CHECK(aica_music_client_poll(&client)==0 && !claimed);cookie=client.cookie;
    transport=2;CHECK(aica_music_client_retry(&client)==0 && client.uncertain && claimed==1);
    CHECK(aica_music_client_retry(&client)==0 && claimed==1 && client.cookie==cookie);
    for(read_error=1;read_error<=3;read_error++) { read_calls=0;CHECK(aica_music_client_poll(&client)==0 && client.state==AICA_MC_WAIT); }
    read_error=0;torn=1;CHECK(aica_music_client_poll(&client)==0 && client.state==AICA_MC_WAIT);
    CHECK(aica_music_client_poll(&client)==1 && claimed==1);
    CHECK(aica_music_client_consume(&client)==0);transport=1;
    command(2,0,0,0,1,0,0);
    active_session=2;CHECK(aica_music_client_quiesce(&client)<0 && client.state==AICA_MC_LOST && refs==1);
}
static void cancellation(void) {
    uint32_t args[4]={1,0,0,0},old;
    initialize();open_port();command(4,0,1,0x90,60,100,4);
    CHECK(aica_music_client_begin(&client,6,0,1,args)==1);
    CHECK(aica_music_client_poll(&client)==0);old=client.cookie;
    transport=3;CHECK(aica_music_client_quiesce(&client)==0 && client.superseded[3]==old && refs==1);
    transport=1;CHECK(aica_music_client_quiesce(&client)==1 && client.cookie==old+1);
    CHECK(aica_music_client_poll(&client)==1 && client.receipt.cancelled_cookie==old && !refs && !starts);
    CHECK(client.superseded[9]==6);
    CHECK(aica_music_client_consume(&client)==0 && !client.superseded[3]);
    /* OPEN completed but ACK unseen: quiesce must retain and then query its
       otherwise unknown generation, including on a nonzero port. */
    initialize();command(1,0,0,15,0,0,0);args[0]=4;args[1]=1;
    CHECK(aica_music_client_begin(&client,2,4,0,args)==1 && refs==1);
    CHECK(aica_music_client_quiesce(&client)==1);
    CHECK(aica_music_client_poll(&client)==1 && refs==0);
    CHECK(aica_music_client_consume(&client)==0);command(10,4,0,0,0,0,0);
    CHECK(client.generation[4]==1);
}
static void malformed(void) {
    const unsigned indexes[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,38,39,40,41,43,44,45,63};
    const uint32_t values[]={UINT32_MAX,2,0,2,99,9,99,0x80000000,2,9,1,1,9,9,9,9,2,4,2,0,65536,2,2,128,128,128,32768,128,128,32768,32768,32768,65,65,65,65,65,65,65,1,1};
    aica_music_receipt_t original;aica_music_client_t saved;uint32_t args[4]={3,1,0,0};
    initialize();command(1,0,0,15,0,0,0);
    CHECK(aica_music_client_begin(&client,2,0,0,args)==1);original=receipt;saved=client;
    CHECK(sizeof(indexes)/sizeof(indexes[0])==sizeof(values)/sizeof(values[0]));
    for(unsigned i=0;i<sizeof(indexes)/sizeof(indexes[0]);i++) {
        uint32_t words[64];receipt=original;client=saved;memcpy(words,&receipt,sizeof(words));words[indexes[i]]=values[i];memcpy(&receipt,words,sizeof(receipt));
        CHECK(aica_music_client_poll(&client)<0 && client.state==AICA_MC_LOST && refs==1);
        CHECK(client.packet[9]==2 && aica_music_client_retry(&client)<0);
    }
    receipt=original;client=saved;receipt.sequence|=1;
    CHECK(aica_music_client_poll(&client)==0 && client.state==AICA_MC_WAIT);
    receipt=original;CHECK(aica_music_client_poll(&client)==1);
    initialize();uint32_t mask[4]={15,0,0,0};
    CHECK(aica_music_client_begin(&client,1,0,0,mask)==1);
    receipt.result=AICA_MUSIC_WIRE_PENDING;
    CHECK(aica_music_client_poll(&client)<0 && claimed==1);
    initialize();open_port();
    CHECK(aica_music_client_quiesce(&client)==1);
    receipt.cancelled_cookie=1; /* No request was superseded. */
    CHECK(aica_music_client_poll(&client)<0);

}
static void ports_and_fades(void) {
    initialize();command(1,0,0,15,0,0,0);
    for(unsigned round=0;round<16;round++) for(unsigned p=0;p<8;p++) {
        uint32_t generation=round+1;
        command(2,p,0,p,generation,0,0);
        CHECK(client.generation[p]==generation && refs==1);
        command(8,p,generation,0,(uint32_t)-100,50,0);
        CHECK(client.receipt.fade_remaining[0]==50);
        command(4,p,generation,0xb0,48,round+p,0);
        CHECK(aica_music_service(&music,50)==1);
        command(10,p,generation,0,0,0,0);
        CHECK(client.receipt.mailbox==round+p && client.receipt.volume==-100 && !client.receipt.fade_remaining[0]);
        command(3,p,generation,0,0,0,0);CHECK(!refs);
    }
}
static void exhaustion_and_validation(void) {
    uint32_t args[4]={0};unsigned calls;
    initialize();calls=read_calls;
    CHECK(aica_music_client_begin(&client,1,0,0,args)<0 && client.state==AICA_MC_IDLE);
    args[0]=1;CHECK(aica_music_client_begin(&client,1,1,0,args)<0);
    CHECK(aica_music_client_begin(&client,14,0,0,args)<0 && read_calls==calls);
    client.cookie=UINT32_MAX;CHECK(aica_music_client_begin(&client,1,0,0,args)<0 && client.state==AICA_MC_LOST);
    initialize();client.cookie=UINT32_MAX-1;
    CHECK(aica_music_client_begin(&client,1,0,0,args)==1);
    CHECK(aica_music_client_poll(&client)==1 && claimed==1);
    CHECK(aica_music_client_consume(&client)==0);
    CHECK(aica_music_client_quiesce(&client)<0 && client.state==AICA_MC_LOST && claimed==1);
    initialize();receipt.cookie=1;
    aica_music_client_ops_t ops=client.ops;CHECK(aica_music_client_init(&client,1,&ops)==0);
    CHECK(aica_music_client_poll(&client)<0); /* no adoption */
}
int main(void) { ordering();uncertainty();cancellation();malformed();ports_and_fades();exhaustion_and_validation();printf("music client checks: %u\n",checks);return 0; }
