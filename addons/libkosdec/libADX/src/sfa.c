#include <kosdec/adx_sfa.h>
#include <string.h>
#define SFA_MAGIC UINT32_C(0x41445848)
static bool valid(const struct kosdec_adx_sfa *s) { return s && s->magic==SFA_MAGIC; }
static enum kosdec_adx_status available(struct kosdec_adx_sfa *s,struct kosdec_adx_joint_info *i) {
    if(kosdec_adx_joint_query(s->joint,i)!=KOSDEC_ADX_JOINT_OK || i->sealed)
        return KOSDEC_ADX_IO_ERROR;
    if(i->borrowed[KOSDEC_ADX_JOINT_FREE]) return KOSDEC_ADX_NEED_OUTPUT;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_sfa_init(struct kosdec_adx_sfa *s,
    struct kosdec_adx_joint *j,unsigned channels,uint32_t rate,uint32_t frames) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_joint_info i;
    if(kosdec_adx_joint_query(j,&i)!=KOSDEC_ADX_JOINT_OK ||
       i.kind!=KOSDEC_ADX_JOINT_RING || i.capacity<36 || i.sealed ||
       i.available[KOSDEC_ADX_JOINT_FREE]!=i.capacity ||
       i.borrowed[0] || i.borrowed[1]) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_sfa next={.magic=SFA_MAGIC,.joint=j,.channels=channels,
        .groups=(uint32_t)(((uint64_t)frames+31)/32)};
    enum kosdec_adx_status status=kosdec_adx_make_sfa_header(next.header,
        sizeof(next.header),channels,rate,frames);
    if(status!=KOSDEC_ADX_DONE) return status;
    *s=next;return KOSDEC_ADX_DONE;
}
/* The exclusive producer preflight guarantees both wrap admissions can commit;
   ring get/put cannot exhaust nodes or fail after admitted valid prefix spans. */
static enum kosdec_adx_status publish(struct kosdec_adx_sfa *s,const uint8_t *data,size_t bytes) {
    while(bytes) {
        struct kosdec_adx_chunk c;
        if(kosdec_adx_joint_get(s->joint,KOSDEC_ADX_JOINT_FREE,bytes,&c)!=KOSDEC_ADX_JOINT_OK || !c.bytes)
            return KOSDEC_ADX_IO_ERROR;
        if(data) memcpy(c.data,data,c.bytes);
        else memset(c.data,0,c.bytes);
        if(kosdec_adx_joint_put(s->joint,&c)!=KOSDEC_ADX_JOINT_OK)
            return KOSDEC_ADX_IO_ERROR;
        bytes-=c.bytes;if(data) data+=c.bytes;
    }
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_sfa_pump(struct kosdec_adx_sfa *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->done) return KOSDEC_ADX_DONE;
    struct kosdec_adx_joint_info i;
    enum kosdec_adx_status status=available(s,&i);
    if(status!=KOSDEC_ADX_MORE) return status;
    static const uint8_t footer[18]={0x80,1,0,14};
    const uint8_t *p;
    unsigned *used,limit;
    if(s->header_used<sizeof(s->header)) {
        p=s->header;used=&s->header_used;limit=sizeof(s->header);
    }
    else if(s->published_groups<s->groups) return KOSDEC_ADX_MORE;
    else { p=footer;used=&s->footer_used;limit=sizeof(footer); }
    size_t bytes=limit-*used;
    if(bytes>i.available[KOSDEC_ADX_JOINT_FREE]) bytes=i.available[KOSDEC_ADX_JOINT_FREE];
    if(!bytes && *used<limit) return KOSDEC_ADX_NEED_OUTPUT;
    status=publish(s,p+*used,bytes);
    if(status!=KOSDEC_ADX_MORE) return status;
    *used+=(unsigned)bytes;
    if(s->footer_used==sizeof(footer)) {
        if(kosdec_adx_joint_finish(s->joint)!=KOSDEC_ADX_JOINT_OK) return KOSDEC_ADX_IO_ERROR;
        s->done=true;return KOSDEC_ADX_DONE;
    }
    return KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status write_groups(struct kosdec_adx_sfa *s,
    const uint8_t *data,uint32_t groups,uint32_t *accepted) {
    if(!valid(s) || !accepted || s->done || groups>s->groups-s->published_groups)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->header_used<sizeof(s->header)) { *accepted=0;return KOSDEC_ADX_NEED_OUTPUT; }
    struct kosdec_adx_joint_info i;
    enum kosdec_adx_status status=available(s,&i);
    if(status!=KOSDEC_ADX_MORE) {
        if(status==KOSDEC_ADX_NEED_OUTPUT) *accepted=0;
        return status;
    }
    size_t width=18*s->channels;
    uint32_t take=groups;
    if(take>512/width) take=(uint32_t)(512/width);
    if(take>i.available[0]/width) take=(uint32_t)(i.available[0]/width);
    if(data) for(size_t n=0;n<(size_t)take*width;n+=18)
        if(data[n]&0x80) return KOSDEC_ADX_INVALID;
    status=publish(s,data,(size_t)take*width);
    if(status!=KOSDEC_ADX_MORE) return status;
    s->published_groups+=take;*accepted=take;
    return take || !groups?KOSDEC_ADX_MORE:KOSDEC_ADX_NEED_OUTPUT;
}
enum kosdec_adx_status kosdec_adx_sfa_write(struct kosdec_adx_sfa *s,
    const uint8_t *data,uint32_t groups,uint32_t *accepted) {
    if(!data && groups) return KOSDEC_ADX_BAD_ARGUMENT;
    return write_groups(s,data,groups,accepted);
}
enum kosdec_adx_status kosdec_adx_sfa_insert(struct kosdec_adx_sfa *s,
    uint32_t groups,uint32_t *accepted) {
    return write_groups(s,NULL,groups,accepted);
}
