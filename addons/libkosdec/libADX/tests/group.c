/* Collection control fault model; physical multi-player behavior is separate. */
#include <kosdec/adx_group.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,polls[32],stops[32];
static uint32_t bad_poll,bad_stop;
static struct kosdec_adx_player players[32];
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"group:%u %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
int kosdec_adx_player_get_info(struct kosdec_adx_player *p,struct kosdec_adx_player_info *out) {
    if(!p || !p->magic) { errno=EINVAL;return -1; }*out=p->info;return 0;
}
int kosdec_adx_player_poll(struct kosdec_adx_player *p) {
    unsigned i=(unsigned)(p-players);CHECK(i<32);++polls[i];
    if(bad_poll&(UINT32_C(1)<<i)) { errno=EIO;return -1; }return 0;
}
int kosdec_adx_player_stop(struct kosdec_adx_player *p,uint32_t timeout) {
    unsigned i=(unsigned)(p-players);CHECK(i<32 && timeout==77);++stops[i];
    if(bad_stop&(UINT32_C(1)<<i)) { errno=EBUSY;return -1; }
    p->info.state=KOSDEC_ADX_PLAYER_STOPPED;p->info.retained_sound=(snd_stream_hnd_t)(i+7);return 0;
}
int main(void) {
    for(unsigned capacity=1;capacity<=32;++capacity) {
        memset(players,0,sizeof(players));memset(polls,0,sizeof(polls));memset(stops,0,sizeof(stops));
        struct kosdec_adx_group group;
        struct kosdec_adx_group_entry workspace[32];uint64_t ids[32];
        CHECK(!kosdec_adx_group_init(&group,workspace,capacity));
        for(unsigned i=0;i<capacity;++i) {
            players[i].magic=1;players[i].info.state=KOSDEC_ADX_PLAYER_PLAYING;
            CHECK(!kosdec_adx_group_add(&group,&players[i],&ids[i]) && ids[i]==i+1);
            CHECK(kosdec_adx_group_add(&group,&players[i],&ids[i])<0 && errno==EEXIST);
        }
        CHECK(kosdec_adx_group_finish(&group)<0 && errno==EBUSY);
        struct kosdec_adx_player_info snapshot;
        CHECK(kosdec_adx_group_remove(&group,ids[0],&snapshot)<0 && errno==EBUSY);
        struct kosdec_adx_group_result result={.visited=123};
        CHECK(kosdec_adx_group_poll(&group,0,&result)<0 && result.visited==123);
        bad_poll=1;
        for(unsigned i=0;i<capacity;++i) {
            int r=kosdec_adx_group_poll(&group,1,&result);
            CHECK(result.visited==(UINT32_C(1)<<i));
            CHECK(i?r==0:r<0);
        }
        for(unsigned i=0;i<capacity;++i) CHECK(polls[i]==1);
        uint32_t mask=UINT32_MAX>>(32-capacity);
        bad_stop=UINT32_C(1)<<(capacity-1);
        CHECK(kosdec_adx_group_stop_all(&group,77,&result)<0 && errno==EBUSY);
        CHECK(result.visited==mask && result.failed==bad_stop);
        for(unsigned i=0;i<capacity;++i) CHECK(stops[i]==1);
        bad_stop=0;
        CHECK(!kosdec_adx_group_stop_all(&group,77,&result) && !result.failed);
        for(unsigned i=0;i<capacity;++i) {
            CHECK(!kosdec_adx_group_remove(&group,ids[i],&snapshot));
            CHECK(snapshot.retained_sound==(snd_stream_hnd_t)(i+7));
            CHECK(kosdec_adx_group_remove(&group,ids[i],&snapshot)<0 && errno==ENOENT);
        }
        CHECK(!kosdec_adx_group_poll(&group,capacity,&result) && !result.visited);
        players[0].info.state=KOSDEC_ADX_PLAYER_READY;
        uint64_t id=0;
        CHECK(!kosdec_adx_group_add(&group,&players[0],&id) && id==capacity+1);
        CHECK(!kosdec_adx_group_remove(&group,id,&snapshot));
        CHECK(!kosdec_adx_group_finish(&group));
    }
    printf("ADX_GROUP checks=%u failures=0\n",checks);
    return 0;
}
