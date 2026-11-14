#include <kosdec/adx_group.h>
#include <errno.h>
#include <string.h>
#define GROUP_MAGIC UINT32_C(0x41445847)
static int fail(int e) { errno=e;return -1; }
static bool valid(const struct kosdec_adx_group *g) { return g && g->magic==GROUP_MAGIC; }
int kosdec_adx_group_init(struct kosdec_adx_group *g,struct kosdec_adx_group_entry *entries,unsigned n) {
    if(!g || !entries || !n || n>KOSDEC_ADX_GROUP_MAX) return fail(EINVAL);
    memset(entries,0,n*sizeof(*entries));
    *g=(struct kosdec_adx_group){.magic=GROUP_MAGIC,.next_id=1,.capacity=n,.entries=entries};
    return 0;
}
int kosdec_adx_group_add(struct kosdec_adx_group *g,struct kosdec_adx_player *p,uint64_t *id) {
    struct kosdec_adx_player_info info;
    if(!valid(g) || !id || !p) return fail(EINVAL);
    if(kosdec_adx_player_get_info(p,&info)<0) return -1;
    unsigned slot=g->capacity;
    for(unsigned i=0;i<g->capacity;++i) {
        if(g->entries[i].player==p) return fail(EEXIST);
        if(!g->entries[i].id && slot==g->capacity) slot=i;
    }
    if(slot==g->capacity) return fail(ENOSPC);
    if(!g->next_id) return fail(EOVERFLOW);
    g->entries[slot]=(struct kosdec_adx_group_entry){g->next_id++,p};
    *id=g->entries[slot].id;
    return 0;
}
static void record(struct kosdec_adx_group_result *r,unsigned slot,int error) {
    r->failed|=UINT32_C(1)<<slot;
    if(!r->first_error) r->first_error=error?error:EIO;
}
int kosdec_adx_group_poll(struct kosdec_adx_group *g,unsigned budget,struct kosdec_adx_group_result *out) {
    if(!valid(g) || !out || !budget || budget>g->capacity) return fail(EINVAL);
    struct kosdec_adx_group_result result={0};
    for(unsigned n=0;n<budget;++n) {
        unsigned i=g->cursor;g->cursor=(i+1)%g->capacity;
        struct kosdec_adx_player *p=g->entries[i].player;
        if(!p) continue;
        result.visited|=UINT32_C(1)<<i;
        struct kosdec_adx_player_info info;
        if(kosdec_adx_player_get_info(p,&info)<0) record(&result,i,errno);
        else if(info.state==KOSDEC_ADX_PLAYER_FAILED || info.state==KOSDEC_ADX_PLAYER_STOPPING)
            record(&result,i,info.stop_error?info.stop_error:info.first_error);
        else if(info.state!=KOSDEC_ADX_PLAYER_READY && info.state!=KOSDEC_ADX_PLAYER_STOPPED &&
                kosdec_adx_player_poll(p)<0) record(&result,i,errno);
    }
    *out=result;
    return result.first_error?fail(result.first_error):0;
}
int kosdec_adx_group_stop_all(struct kosdec_adx_group *g,uint32_t timeout,
    struct kosdec_adx_group_result *out) {
    if(!valid(g) || !out || !timeout) return fail(EINVAL);
    struct kosdec_adx_group_result result={0};
    for(unsigned i=0;i<g->capacity;++i) if(g->entries[i].player) {
        result.visited|=UINT32_C(1)<<i;
        if(kosdec_adx_player_stop(g->entries[i].player,timeout)<0) record(&result,i,errno);
    }
    *out=result;
    return result.first_error?fail(result.first_error):0;
}
int kosdec_adx_group_remove(struct kosdec_adx_group *g,uint64_t id,struct kosdec_adx_player_info *out) {
    if(!valid(g) || !id || !out) return fail(EINVAL);
    for(unsigned i=0;i<g->capacity;++i) if(g->entries[i].id==id) {
        struct kosdec_adx_player_info info;
        if(kosdec_adx_player_get_info(g->entries[i].player,&info)<0) return -1;
        if(info.state!=KOSDEC_ADX_PLAYER_READY && info.state!=KOSDEC_ADX_PLAYER_STOPPED)
            return fail(EBUSY);
        *out=info;
        g->entries[i]=(struct kosdec_adx_group_entry){0};
        return 0;
    }
    return fail(ENOENT);
}
int kosdec_adx_group_finish(struct kosdec_adx_group *g) {
    if(!valid(g)) return fail(EINVAL);
    for(unsigned i=0;i<g->capacity;++i) if(g->entries[i].id) return fail(EBUSY);
    g->magic=0;
    return 0;
}
