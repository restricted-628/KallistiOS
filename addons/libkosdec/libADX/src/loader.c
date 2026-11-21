#include "adx_loader_internal.h"
#include <string.h>
#include <math.h>
#define LOADER_MAGIC UINT32_C(0x4144584c)
#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
#define LSC_MAX_REFILL_SECTORS 512u
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
    ++e->info.completed_passes;
    l->head=(l->head+1)%KOSDEC_ADX_LOADER_SOURCES;
    if(l->repeat) {
        struct kosdec_adx_loader_entry next = *e;
        *e = (struct kosdec_adx_loader_entry){0};
        next.info.state = KOSDEC_ADX_LOADER_QUEUED;
        next.info.read_sectors = next.info.published_bytes = 0;
        next.completed_sectors = next.completed_bytes = 0;
        l->entries[l->tail] = next;
        l->tail = (l->tail + 1) % KOSDEC_ADX_LOADER_SOURCES;
        return;
    }
    e->info.state=KOSDEC_ADX_LOADER_END;e->read_at=NULL;e->context=NULL;
    if(!--l->count)l->state=KOSDEC_ADX_LOADER_WAIT;
}

static size_t effective_flow_limit(const struct kosdec_adx_loader *l) {
    if(l->lsc_flow && l->count && l->entries[l->head].info.state == KOSDEC_ADX_LOADER_READING)
        return l->source_flow_limit;
    return l->flow_limit;
}

static uint32_t lsc_refill_sectors(const struct kosdec_adx_loader_entry *e, size_t bytes) {
    uint32_t remaining = e->info.sectors - e->completed_sectors;
    if(remaining <= LSC_MAX_REFILL_SECTORS &&
       e->info.data_bytes - e->completed_bytes <= bytes)
        return remaining;
    size_t sectors = bytes / SECTOR;
    return sectors < LSC_MAX_REFILL_SECTORS ? (uint32_t)sectors : LSC_MAX_REFILL_SECTORS;
}

static enum kosdec_adx_loader_status admit_read(struct kosdec_adx_loader *l,
                                               struct kosdec_adx_loader_entry *e) {
    struct kosdec_adx_joint_info joint;
    if(kosdec_adx_joint_query(l->joint,&joint)!=KOSDEC_ADX_JOINT_OK ||
       joint.sealed || joint.capacity!=l->capacity)
        return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
    if(joint.borrowed[KOSDEC_ADX_JOINT_FREE])
        return KOSDEC_ADX_LOADER_BUSY;
    if(l->lsc_flow && e->info.state == KOSDEC_ADX_LOADER_QUEUED) {
        l->source_flow_limit = l->flow_limit;
        e->info.state = KOSDEC_ADX_LOADER_READING;
    }
    size_t buffered=joint.available[KOSDEC_ADX_JOINT_DATA];
    if(!l->sector_policy && !l->lsc_flow)
        buffered+=joint.borrowed[KOSDEC_ADX_JOINT_DATA];
    size_t limit = effective_flow_limit(l);
    if((limit || l->sector_policy || l->lsc_flow) && buffered >= limit)
        return KOSDEC_ADX_LOADER_NEED_OUTPUT;
    uint32_t sectors = 1;
    if(l->lsc_flow) {
        sectors = lsc_refill_sectors(e, joint.contiguous[KOSDEC_ADX_JOINT_FREE]);
        if(!sectors) {
            /* Native joints can split a single sector over a wrap or nodes. */
            if(!lsc_refill_sectors(e, joint.available[KOSDEC_ADX_JOINT_FREE]))
                return KOSDEC_ADX_LOADER_NEED_OUTPUT;
            sectors = 1;
        }
    }
    (void)kosdec_adx_request_init(&l->request,e->read_at,e->context,e->input_bytes);
    l->active=true;
    if(kosdec_adx_request_seek(&l->request,(int64_t)e->info.start_sector+e->completed_sectors,
                             KOSDEC_ADX_REQUEST_SET)!=KOSDEC_ADX_REQUEST_STOPPED)
        return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
    enum kosdec_adx_request_status status = kosdec_adx_request_start_joint(&l->request,sectors,l->joint);
    if(status!=KOSDEC_ADX_REQUEST_READING)
        return failed(l,status);
    e->info.state=KOSDEC_ADX_LOADER_READING;
    return KOSDEC_ADX_LOADER_OK;
}

static enum kosdec_adx_loader_status advance_read(struct kosdec_adx_loader *l,
                                                 struct kosdec_adx_loader_entry *e) {
    enum kosdec_adx_request_status status=kosdec_adx_request_step(&l->request);
    struct kosdec_adx_request_info info;
    if(!kosdec_adx_request_get_info(&l->request,&info))
        return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
    e->info.read_sectors=e->completed_sectors+info.completed;
    e->info.published_bytes=e->completed_bytes+(uint32_t)info.output_bytes;
    if(status==KOSDEC_ADX_REQUEST_READEND || status==KOSDEC_ADX_REQUEST_READING)
        return KOSDEC_ADX_LOADER_OK;
    if(status==KOSDEC_ADX_REQUEST_WAIT)
        return KOSDEC_ADX_LOADER_NEED_INPUT;
    if(status==KOSDEC_ADX_REQUEST_NEED_OUTPUT)
        return KOSDEC_ADX_LOADER_NEED_OUTPUT;
    return failed(l,status);
}

static enum kosdec_adx_loader_status finish_read(struct kosdec_adx_loader *l,
                                                struct kosdec_adx_loader_entry *e) {
    if(kosdec_adx_request_close(&l->request)!=KOSDEC_ADX_REQUEST_STOPPED)
        return failed(l,KOSDEC_ADX_REQUEST_IO_ERROR);
    l->active=false;
    e->completed_sectors=e->info.read_sectors;
    e->completed_bytes=e->info.published_bytes;
    if(e->completed_sectors==e->info.sectors)
        complete(l,e);
    return KOSDEC_ADX_LOADER_OK;
}

enum loader_step_mode { LOADER_COMBINED, LOADER_STREAM, LOADER_RELOAD };

static enum kosdec_adx_loader_status service(struct kosdec_adx_loader *l,
                                            enum loader_step_mode mode) {
    if(!valid(l))
        return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state==KOSDEC_ADX_LOADER_FAILED)
        return KOSDEC_ADX_LOADER_IO_ERROR;
    if(l->paused || l->state!=KOSDEC_ADX_LOADER_EXEC)
        return KOSDEC_ADX_LOADER_OK;

    struct kosdec_adx_loader_entry *e=&l->entries[l->head];
    if(mode == LOADER_RELOAD && l->active) {
        if(l->request.state != KOSDEC_ADX_REQUEST_READEND)
            return KOSDEC_ADX_LOADER_OK;
        enum kosdec_adx_loader_status status=finish_read(l,e);
        if(status!=KOSDEC_ADX_LOADER_OK || l->state!=KOSDEC_ADX_LOADER_EXEC)
            return status;
        e=&l->entries[l->head];
        /* Only one entry can finish this visit, including zero-length entries. */
        return e->completed_sectors==e->info.sectors ?
            KOSDEC_ADX_LOADER_OK : admit_read(l,e);
    }
    if(!l->active) {
        if(mode == LOADER_STREAM)
            return KOSDEC_ADX_LOADER_OK;
        if(e->completed_sectors==e->info.sectors) {
            complete(l,e);
            return KOSDEC_ADX_LOADER_OK;
        }
        enum kosdec_adx_loader_status status=admit_read(l,e);
        if(status!=KOSDEC_ADX_LOADER_OK || mode == LOADER_RELOAD)
            return status;
    }
    if(l->request.state != KOSDEC_ADX_REQUEST_READEND) {
        enum kosdec_adx_loader_status status=advance_read(l,e);
        if(status!=KOSDEC_ADX_LOADER_OK)
            return status;
    }
    return mode == LOADER_COMBINED && l->request.state == KOSDEC_ADX_REQUEST_READEND ?
        finish_read(l,e) : KOSDEC_ADX_LOADER_OK;
}

enum kosdec_adx_loader_status kosdec_adx_loader_step(struct kosdec_adx_loader *l) {
    return service(l,LOADER_COMBINED);
}

enum kosdec_adx_loader_status kosdec_adx_loader_step_stream(struct kosdec_adx_loader *l) {
    return service(l,LOADER_STREAM);
}

enum kosdec_adx_loader_status kosdec_adx_loader_step_reload(struct kosdec_adx_loader *l) {
    return service(l,LOADER_RELOAD);
}

enum kosdec_adx_loader_status kosdec_adx_loader_pause(struct kosdec_adx_loader *l,bool pause) {
    if(!valid(l))return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->paused=pause;return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_repeat(struct kosdec_adx_loader *l, bool repeat) {
    if(!valid(l)) return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state == KOSDEC_ADX_LOADER_FAILED) return KOSDEC_ADX_LOADER_IO_ERROR;
    l->repeat = repeat;
    return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_flow(struct kosdec_adx_loader *l,size_t bytes) {
    if(!valid(l) || bytes>l->capacity)return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->flow_limit=bytes;l->sector_policy=false;l->lsc_flow=false;return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_lsc_flow(
    struct kosdec_adx_loader *l, size_t bytes) {
    if(!valid(l) || l->capacity < SECTOR || bytes > l->capacity)
        return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(!l->lsc_flow && l->state != KOSDEC_ADX_LOADER_STOPPED)
        return KOSDEC_ADX_LOADER_BUSY;
    l->flow_limit = bytes;
    l->sector_policy = false;
    l->lsc_flow = true;
    return KOSDEC_ADX_LOADER_OK;
}
static bool sector_capacity(const struct kosdec_adx_loader *l) {
    return valid(l) && l->capacity/SECTOR && l->capacity/SECTOR<=INT16_MAX;
}
static bool adx_format(unsigned channels,unsigned rate) {
    return (channels==1 || channels==2) && rate && rate<=48000;
}
uint32_t media_adx_default_reload_sectors(size_t capacity) {
    return (uint32_t)(int32_t)((float)(capacity / SECTOR) * 0.85f);
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_sectors(
    struct kosdec_adx_loader *l,uint32_t sectors) {
    if(!sector_capacity(l) || sectors>l->capacity/SECTOR)
        return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    l->flow_limit=(size_t)sectors*SECTOR;l->sector_policy=true;l->lsc_flow=false;
    return KOSDEC_ADX_LOADER_OK;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_default(
    struct kosdec_adx_loader *l) {
    if(!sector_capacity(l)) return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    return kosdec_adx_loader_set_reload_sectors(l,media_adx_default_reload_sectors(l->capacity));
}
bool media_adx_reload_time_sectors(float seconds, unsigned channels,
    unsigned rate, uint32_t capacity_sectors, uint32_t *out) {
    if(!adx_format(channels,rate) || !isfinite(seconds) || seconds<0.0f) return false;
    float samples=(float)rate*seconds;
    uint32_t sectors=capacity_sectors;
    if(samples<0x1p31f) {
        uint32_t frames=(uint32_t)(int32_t)samples;
        uint64_t requested=(uint64_t)(frames/32)*18*channels/SECTOR;
        if(requested<sectors) sectors=(uint32_t)requested;
    }
    *out = sectors;
    return true;
}
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_time(
    struct kosdec_adx_loader *l,float seconds,unsigned channels,unsigned rate) {
    uint32_t sectors;
    if(!sector_capacity(l) ||
       !media_adx_reload_time_sectors(seconds,channels,rate,(uint32_t)(l->capacity/SECTOR),&sectors))
        return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    return kosdec_adx_loader_set_reload_sectors(l,sectors);
}
enum kosdec_adx_loader_status media_adx_loader_start_linked(
    struct kosdec_adx_loader *l, uint32_t sectors, bool clamp) {
    if(!sector_capacity(l)) return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    if(l->state != KOSDEC_ADX_LOADER_STOPPED) return KOSDEC_ADX_LOADER_BUSY;
    struct kosdec_adx_joint_info joint;
    if(kosdec_adx_joint_query(l->joint,&joint) != KOSDEC_ADX_JOINT_OK ||
       joint.sealed || joint.capacity != l->capacity ||
       joint.borrowed[KOSDEC_ADX_JOINT_FREE] || joint.borrowed[KOSDEC_ADX_JOINT_DATA] ||
       joint.available[KOSDEC_ADX_JOINT_DATA])
        return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
    uint32_t capacity_sectors = (uint32_t)(l->capacity / SECTOR);
    if(sectors == MEDIA_ADX_RELOAD_DEFAULT) sectors = media_adx_default_reload_sectors(l->capacity);
    if(sectors > capacity_sectors) {
        if(!clamp) return KOSDEC_ADX_LOADER_BAD_ARGUMENT;
        sectors = capacity_sectors;
    }
    enum kosdec_adx_loader_status status = kosdec_adx_loader_set_lsc_flow(l,(size_t)sectors * SECTOR);
    if(status != KOSDEC_ADX_LOADER_OK) return status;
    return kosdec_adx_loader_start(l);
}
float media_adx_input_seconds(uint32_t bytes, unsigned channels, unsigned rate) {
    uint32_t frames = (bytes / (18 * channels)) * 32;
    return (float)frames / (float)rate;
}
bool kosdec_adx_loader_get_buffer_info(const struct kosdec_adx_loader *l,
    unsigned channels,unsigned rate,struct kosdec_adx_loader_buffer_info *out) {
    struct kosdec_adx_joint_info info;
    if(!valid(l) || !out || !adx_format(channels,rate) ||
       kosdec_adx_joint_query(l->joint,&info)!=KOSDEC_ADX_JOINT_OK ||
       info.capacity!=l->capacity) return false;
    size_t bytes=info.available[KOSDEC_ADX_JOINT_DATA];
    size_t limit = effective_flow_limit(l);
    *out=(struct kosdec_adx_loader_buffer_info){
        .queued_bytes=bytes,.held_bytes=info.borrowed[KOSDEC_ADX_JOINT_DATA],
        .reload_bytes=limit,.sectors=(uint32_t)(bytes/SECTOR),
        .seconds=media_adx_input_seconds((uint32_t)bytes,channels,rate),.sector_policy=l->sector_policy,
        .safe=bytes>=limit
    };
    return true;
}
struct kosdec_adx_joint *kosdec_adx_loader_get_input_joint(
    const struct kosdec_adx_loader *l) {
    return valid(l)?l->joint:NULL;
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
