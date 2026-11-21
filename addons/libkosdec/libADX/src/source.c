#include <kosdec/adx_source.h>
#include <kosdec/adx_joint.h>
#include <string.h>
#define SOURCE_MAGIC UINT32_C(0x41445842)
static bool valid(const struct kosdec_adx_source_handle *s) {
    return s && s->magic==SOURCE_MAGIC;
}
static struct kosdec_adx_input_result memory_read(void *context,uint32_t offset,
                                                uint8_t *out,size_t capacity) {
    struct kosdec_adx_source_handle *s=context;
    if(!valid(s) || offset>s->parent.bytes || (!out && capacity))
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(offset==s->parent.bytes)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
    if(capacity>s->parent.bytes-offset) capacity=s->parent.bytes-offset;
    if(capacity>512) capacity=512;
    if(capacity) memcpy(out,s->memory+offset,capacity);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,capacity};
}
static struct kosdec_adx_input_result loan_read(void *context,uint32_t offset,
                                              uint8_t *out,size_t capacity) {
    struct kosdec_adx_source_handle *s=context;
    if(!valid(s) || !s->loan)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    return kosdec_adx_view_read_at(&s->selected,offset,out,capacity);
}
enum kosdec_adx_status kosdec_adx_source_init(struct kosdec_adx_source_handle *s,
    kosdec_adx_read_at read_at,void *context,uint32_t bytes) {
    if(!s || !read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_source_handle initial={.magic=SOURCE_MAGIC,
        .parent={.read_at=read_at,.context=context,.bytes=bytes}};
    (void)kosdec_adx_view_init(&initial.selected,read_at,context,bytes,0,bytes);
    *s=initial;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_source_init_memory(struct kosdec_adx_source_handle *s,
    const uint8_t *memory,uint32_t bytes) {
    if(!s || (!memory && bytes)) return KOSDEC_ADX_BAD_ARGUMENT;
    (void)kosdec_adx_source_init(s,memory_read,s,bytes);
    s->memory=memory;
    return KOSDEC_ADX_DONE;
}
static enum kosdec_adx_status available(const struct kosdec_adx_source_handle *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->loan || s->selecting) return KOSDEC_ADX_NEED_OUTPUT;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_source_select_range(struct kosdec_adx_source_handle *s,
    uint32_t offset,uint32_t bytes) {
    enum kosdec_adx_status status=available(s);
    if(status!=KOSDEC_ADX_DONE) return status;
    return kosdec_adx_view_init(&s->selected,s->parent.read_at,s->parent.context,
                              s->parent.bytes,offset,bytes);
}
enum kosdec_adx_status kosdec_adx_source_index_begin(struct kosdec_adx_source_handle *s,
    enum kosdec_adx_container format,uint32_t ordinal) {
    enum kosdec_adx_status status=available(s);
    if(status!=KOSDEC_ADX_DONE) return status;
    if(format!=KOSDEC_ADX_ACX && format!=KOSDEC_ADX_AFS) return KOSDEC_ADX_BAD_ARGUMENT;
    kosdec_adx_index_init(&s->index,s->parent.read_at,s->parent.context,s->parent.bytes,format,ordinal);
    s->selecting=true;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_source_index_step(struct kosdec_adx_source_handle *s) {
    if(!valid(s) || !s->selecting) return KOSDEC_ADX_BAD_ARGUMENT;
    enum kosdec_adx_status status=kosdec_adx_index_step(&s->index);
    if(status==KOSDEC_ADX_DONE) s->selected=s->index.member;
    if(status>=KOSDEC_ADX_DONE) s->selecting=false;
    return status;
}
enum kosdec_adx_status kosdec_adx_source_index_cancel(struct kosdec_adx_source_handle *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->selecting) kosdec_adx_index_cancel(&s->index);
    s->selecting=false;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_source_borrow(struct kosdec_adx_source_handle *s,
    struct kosdec_adx_source *out,uint32_t tag) {
    enum kosdec_adx_status status=available(s);
    if(status!=KOSDEC_ADX_DONE) return status;
    if(!out || out->read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    *out=(struct kosdec_adx_source){.read_at=loan_read,.context=s,.bytes=s->selected.bytes,.tag=tag};
    s->loan=out;
    return KOSDEC_ADX_DONE;
}
static bool owns_loan(const struct kosdec_adx_source_handle *s,
                      const struct kosdec_adx_source *loan) {
    return valid(s) && loan && loan == s->loan && loan->read_at == loan_read &&
        loan->context == s && loan->bytes == s->selected.bytes;
}
enum kosdec_adx_status kosdec_adx_source_init_memory_joint(
    const struct kosdec_adx_source_handle *s, const struct kosdec_adx_source *loan,
    struct kosdec_adx_joint *joint) {
    if(!joint || !owns_loan(s,loan) || !loan->bytes)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(!s->memory) return KOSDEC_ADX_UNSUPPORTED;
    /* The memory joint offers read-only DATA and never writes the backing. */
    const uint8_t *data = s->memory + s->selected.offset;
    if(kosdec_adx_joint_memory_init(joint,(void *)data,loan->bytes) != KOSDEC_ADX_JOINT_OK)
        return KOSDEC_ADX_BAD_ARGUMENT;
    (void)kosdec_adx_joint_finish(joint);
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_source_return(struct kosdec_adx_source_handle *s,
    struct kosdec_adx_source *loan) {
    if(!owns_loan(s,loan)) return KOSDEC_ADX_BAD_ARGUMENT;
    s->loan=NULL;
    *loan=(struct kosdec_adx_source){0};
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_source_close(struct kosdec_adx_source_handle *s) {
    enum kosdec_adx_status status=available(s);
    if(status!=KOSDEC_ADX_DONE) return status;
    s->magic=0;
    return KOSDEC_ADX_DONE;
}
