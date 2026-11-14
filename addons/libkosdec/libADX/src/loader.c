#include <kosdec/adx_loader.h>
#include <string.h>
#define LOADER_MAGIC UINT32_C(0x4144584c)
#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
static bool valid(const struct kosdec_adx_loader *l){return l && l->magic==LOADER_MAGIC;}
enum kosdec_adx_loader_status kosdec_adx_loader_init(struct kosdec_adx_loader *l,
                                                   struct kosdec_adx_joint *joint) {
    struct kosdec_adx_joint_info info;
    if(!l || kosdec_adx_joint_query(joint,&info)!=KOSDEC_ADX_JOINT_OK || info.sealed ||
       info.kind==KOSDEC_ADX_JOINT_MEMORY || info.borrowed[KOSDEC_ADX_JOINT_FREE]) return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    memset(l,0,sizeof(*l));l->magic=LOADER_MAGIC;l->joint=joint;l->capacity=info.capacity;
    l->flow_limit=(size_t)((uint64_t)info.capacity*8/10);l->next_id=1;
    return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_enqueue(struct kosdec_adx_loader *l,
    const char *name,kosdec_adx_read_at read_at,void *ctx,uint32_t bytes,
    uint32_t start,uint32_t count,uint64_t *id) {
    if(!valid(l) || !name || !read_at || !id)return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state==KOSDEC_ADX_LOADER_FAILED)return KOSDEC_ADX_LOADER_IO_ERROR;
    unsigned len=0;while(len<KOSDEC_ADX_LOADER_NAME_BYTES && name[len])++len;
    if(len==KOSDEC_ADX_LOADER_NAME_BYTES)return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->count==KOSDEC_ADX_LOADER_SOURCES || !l->next_id)return KOSDEC_ADX_LOADER_LIMIT;
    uint32_t sectors=bytes/SECTOR+(bytes%SECTOR!=0);
    if(start>sectors)start=sectors;
    if(count>sectors-start)count=sectors-start;
    uint64_t offset=(uint64_t)start*SECTOR, size=(uint64_t)count*SECTOR;
    uint32_t remaining=offset<bytes?bytes-(uint32_t)offset:0;
    if(size>remaining)size=remaining;
    struct kosdec_adx_loader_entry *e=&l->entries[l->tail];
    memset(e,0,sizeof(*e));e->info.id=l->next_id++;memcpy(e->info.name,name,len+1);
    e->info.start_sector=start;e->info.sectors=count;e->info.data_bytes=(uint32_t)size;
    e->read_at=read_at;e->context=ctx;e->input_bytes=bytes;
    *id=e->info.id;l->tail=(l->tail+1)%KOSDEC_ADX_LOADER_SOURCES;++l->count;
    if(l->state==KOSDEC_ADX_LOADER_WAIT)l->state=KOSDEC_ADX_LOADER_EXEC;
    return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_start(struct kosdec_adx_loader *l) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state!=KOSDEC_ADX_LOADER_STOPPED)return KOSDEC_ADX_LOADER_BUSY;
    l->state=l->count?KOSDEC_ADX_LOADER_EXEC:KOSDEC_ADX_LOADER_WAIT;return KOSDEC_ADX_LOADER_OK;
}
static enum kosdec_adx_loader_status failed(struct kosdec_adx_loader *l,enum kosdec_adx_request_status status) {
    if(l->state!=KOSDEC_ADX_LOADER_FAILED) {
        l->state=KOSDEC_ADX_LOADER_FAILED;l->error=status;
        if(l->error_fn)l->error_fn(l->error_context,l->entries[l->head].info.id,status);
    }
    return KOSDEC_ADX_LOADER_IO_ERROR;
}
static void complete(struct kosdec_adx_loader *l,struct kosdec_adx_loader_entry *e) {
    e->info.state=KOSDEC_ADX_LOADER_END;e->read_at=NULL;e->context=NULL;
    l->head=(l->head+1)%KOSDEC_ADX_LOADER_SOURCES;--l->count;
    if(!l->count)l->state=KOSDEC_ADX_LOADER_WAIT;
}
enum kosdec_adx_loader_status kosdec_adx_loader_step(struct kosdec_adx_loader *l) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state==KOSDEC_ADX_LOADER_FAILED)return KOSDEC_ADX_LOADER_IO_ERROR;
    if(l->paused || l->state!=KOSDEC_ADX_LOADER_EXEC)return KOSDEC_ADX_LOADER_OK;
    struct kosdec_adx_loader_entry *e=&l->entries[l->head];
    if(!l->active) {
        if(e->completed_sectors==e->info.sectors){complete(l,e);return KOSDEC_ADX_LOADER_OK;}
        struct kosdec_adx_joint_info joint;
        if(kosdec_adx_joint_query(l->joint,&joint)!=KOSDEC_ADX_JOINT_OK || joint.sealed || joint.capacity!=l->capacity)
            return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
        if(joint.borrowed[KOSDEC_ADX_JOINT_FREE])return KOSDEC_ADX_LOADER_BUSY;
        if(l->flow_limit && joint.available[KOSDEC_ADX_JOINT_DATA]+joint.borrowed[KOSDEC_ADX_JOINT_DATA]>=l->flow_limit)
            return KOSDEC_ADX_LOADER_NEED_OUTPUT;
        (void)kosdec_adx_request_init(&l->request,e->read_at,e->context,e->input_bytes);
        l->active=true;
        if(kosdec_adx_request_seek(&l->request,(int64_t)e->info.start_sector+e->completed_sectors,KOSDEC_ADX_REQUEST_SET)!=KOSDEC_ADX_REQUEST_STOPPED)
            return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
        enum kosdec_adx_request_status s=kosdec_adx_request_start_joint(&l->request,1,l->joint);
        if(s!=KOSDEC_ADX_REQUEST_READING)return failed(l,s);
        e->info.state=KOSDEC_ADX_LOADER_READING;
    }
    enum kosdec_adx_request_status status=kosdec_adx_request_step(&l->request);
    struct kosdec_adx_request_info info;
    if(!kosdec_adx_request_get_info(&l->request,&info))return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
    e->info.read_sectors=e->completed_sectors+info.completed;
    e->info.published_bytes=e->completed_bytes+(uint32_t)info.output_bytes;
    if(status==KOSDEC_ADX_REQUEST_READEND) {
        if(kosdec_adx_request_close(&l->request)!=KOSDEC_ADX_REQUEST_STOPPED)return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
        l->active=false;e->completed_sectors=e->info.read_sectors;e->completed_bytes=e->info.published_bytes;
        if(e->completed_sectors==e->info.sectors)complete(l,e);
        return KOSDEC_ADX_LOADER_OK;
    }
    if(status==KOSDEC_ADX_REQUEST_READING)return KOSDEC_ADX_LOADER_OK;
    if(status==KOSDEC_ADX_REQUEST_WAIT)return KOSDEC_ADX_LOADER_NEED_INPUT;
    if(status==KOSDEC_ADX_REQUEST_NEED_OUTPUT)return KOSDEC_ADX_LOADER_NEED_OUTPUT;
    return failed(l,status);
}
enum kosdec_adx_loader_status kosdec_adx_loader_pause(struct kosdec_adx_loader *l,bool pause) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->paused=pause;return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_flow(struct kosdec_adx_loader *l,size_t bytes) {
    if(!valid(l) || bytes>l->capacity)return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->flow_limit=bytes;return KOSDEC_ADX_LOADER_OK;
}
bool kosdec_adx_loader_get_info(const struct kosdec_adx_loader *l,struct kosdec_adx_loader_info *out) {
    if(!valid(l) || !out)return false;
    *out=(struct kosdec_adx_loader_info){l->state,l->error,l->count,l->flow_limit,l->paused};return true;
}
bool kosdec_adx_loader_get_source(const struct kosdec_adx_loader *l,uint64_t id,struct kosdec_adx_loader_source_info *out) {
    if(!valid(l) || !id || !out)return false;
    for(unsigned i=0;i<KOSDEC_ADX_LOADER_SOURCES;++i)if(l->entries[i].info.id==id){*out=l->entries[i].info;return true;}
    return false;
}
bool kosdec_adx_loader_get_id(const struct kosdec_adx_loader *l,unsigned ordinal,uint64_t *out) {
    if(!valid(l) || !out || ordinal>=l->count)return false;
    *out=l->entries[(l->head+ordinal)%KOSDEC_ADX_LOADER_SOURCES].info.id;return true;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_error(struct kosdec_adx_loader *l,kosdec_adx_loader_error fn,void *ctx) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->error_fn=fn;l->error_context=fn?ctx:NULL;return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_stop(struct kosdec_adx_loader *l) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->active) {
        if(kosdec_adx_request_close(&l->request)!=KOSDEC_ADX_REQUEST_STOPPED)return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
        l->active=false;
    }
    for(unsigned i=0;i<l->count;++i) {
        struct kosdec_adx_loader_entry *e=&l->entries[(l->head+i)%KOSDEC_ADX_LOADER_SOURCES];
        e->info.state=KOSDEC_ADX_LOADER_CANCELLED;e->read_at=NULL;e->context=NULL;
    }
    l->count=l->head=l->tail=0;l->paused=false;l->state=KOSDEC_ADX_LOADER_STOPPED;l->error=KOSDEC_ADX_REQUEST_STOPPED;
    return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_close(struct kosdec_adx_loader *l) {
    enum kosdec_adx_loader_status s=kosdec_adx_loader_stop(l);
    if(s==KOSDEC_ADX_LOADER_OK)memset(l,0,sizeof(*l));
    return s;
}
