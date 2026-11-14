#include <kosdec/adx_files.h>
#include <errno.h>
#include <string.h>
#define FILES_MAGIC UINT32_C(0x41445846)
static int fail(int e) { errno=e;return -1; }
static bool valid(const struct kosdec_adx_files *f) { return f && f->magic==FILES_MAGIC; }
static struct kosdec_adx_files_entry *find(const struct kosdec_adx_files *f,uint64_t id) {
    if(!valid(f) || !id) { fail(EINVAL);return NULL; }
    for(unsigned i=0;i<f->capacity;++i) if(f->entries[i].id==id) return &f->entries[i];
    fail(ENOENT);return NULL;
}
int kosdec_adx_files_init(struct kosdec_adx_files *f,
    struct kosdec_adx_files_entry *entries,unsigned capacity) {
    if(!f || !entries || !capacity || capacity>KOSDEC_ADX_FILES_MAX) return fail(EINVAL);
    memset(entries,0,capacity*sizeof(*entries));
    *f=(struct kosdec_adx_files){.magic=FILES_MAGIC,.next_id=1,.capacity=capacity,.entries=entries};
    return 0;
}
int kosdec_adx_files_open(struct kosdec_adx_files *f,const char *path,
    uint8_t *cache,size_t cache_bytes,uint64_t *id,int *cleanup_error) {
    if(!valid(f) || !path || !*path || !id || !cleanup_error) return fail(EINVAL);
    size_t n=strnlen(path,KOSDEC_ADX_FILENAME_BYTES);
    if(n==KOSDEC_ADX_FILENAME_BYTES) return fail(ENAMETOOLONG);
    if(!f->next_id) return fail(EOVERFLOW);
    unsigned slot=0;
    while(slot<f->capacity && f->entries[slot].id) ++slot;
    if(slot==f->capacity) return fail(ENOSPC);
    struct kosdec_adx_named_file *file=NULL;
    if(kosdec_adx_named_file_open(&file,path,false,cache,cache_bytes,cleanup_error)<0) return -1;
    struct kosdec_adx_files_entry *entry=&f->entries[slot];
    entry->file=file;entry->id=f->next_id++;
    memcpy(entry->name,path,n+1);
    *id=entry->id;
    return 0;
}
int kosdec_adx_files_borrow(struct kosdec_adx_files *f,uint64_t id,
    struct kosdec_adx_source *out,uint32_t tag) {
    struct kosdec_adx_files_entry *entry=find(f,id);
    return entry?kosdec_adx_named_file_borrow_source(entry->file,out,tag):-1;
}
int kosdec_adx_files_return(struct kosdec_adx_files *f,uint64_t id,
    struct kosdec_adx_source *loan) {
    struct kosdec_adx_files_entry *entry=find(f,id);
    return entry?kosdec_adx_named_file_return_source(entry->file,loan):-1;
}
int kosdec_adx_files_get_name(const struct kosdec_adx_files *f,uint64_t id,char *out,size_t capacity) {
    struct kosdec_adx_files_entry *entry=find(f,id);
    if(!entry) return -1;
    if(!out) return fail(EINVAL);
    size_t bytes=strlen(entry->name)+1;
    if(capacity<bytes) return fail(ERANGE);
    memcpy(out,entry->name,bytes);
    return 0;
}
int kosdec_adx_files_close(struct kosdec_adx_files *f,uint64_t id,int *cleanup_error) {
    struct kosdec_adx_files_entry *entry=find(f,id);
    if(!entry) return -1;
    if(kosdec_adx_named_file_close(&entry->file,cleanup_error)<0) return -1;
    entry->id=0;entry->name[0]=0;
    return 0;
}
int kosdec_adx_files_close_all(struct kosdec_adx_files *f,struct kosdec_adx_files_result *out) {
    if(!valid(f) || !out) return fail(EINVAL);
    struct kosdec_adx_files_result result={0};
    for(unsigned i=0;i<f->capacity;++i) if(f->entries[i].id) {
        int cleanup=0;
        if(kosdec_adx_files_close(f,f->entries[i].id,&cleanup)<0) {
            result.retained|=UINT32_C(1)<<i;
            if(!result.first_error) result.first_error=errno?errno:EIO;
        }
        else {
            result.closed|=UINT32_C(1)<<i;
            if(cleanup) {
                result.cleanup_failed|=UINT32_C(1)<<i;
                if(!result.first_error) result.first_error=cleanup;
            }
        }
    }
    *out=result;
    return result.first_error?fail(result.first_error):0;
}
int kosdec_adx_files_finish(struct kosdec_adx_files *f) {
    if(!valid(f)) return fail(EINVAL);
    for(unsigned i=0;i<f->capacity;++i) if(f->entries[i].id) return fail(EBUSY);
    f->magic=0;
    return 0;
}
