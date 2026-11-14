#include <kosdec/adx_files.h>
#include <kosdec/adx_source.h>
#include <kosdec/adx_partition.h>
#include <kosdec/adx_loader.h>
#include <kosdec/adx_joint_reader.h>
#include <kos/fs.h>
#include <kos/thread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks,opens,closes;
static int close_failure=-1,last_fd=-1;
static kthread_t owner;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"files:%u %s errno=%d\n",(unsigned)__LINE__,#x,errno); abort(); } } while(0)
kthread_t *thd_get_current(void) { return &owner; }
int irq_inside_int(void) { return 0; }
uint32_t kosdec_adx_named_test_sr(void) { return 0; }
file_t fs_open(const char *path,int flags) { ++opens;last_fd=open(path,flags);return last_fd; }
int fs_close(file_t fd) { ++closes;int r=close(fd);if(fd==close_failure) { errno=EIO;return -1; }return r; }
int64_t fs_total64(file_t fd) { struct stat st;return fstat(fd,&st)?-1:st.st_size; }
_off64_t fs_seek64(file_t fd,_off64_t off,int whence) { return lseek(fd,off,whence); }
ssize_t fs_read(file_t fd,void *out,size_t bytes) { return read(fd,out,bytes); }
static enum kosdec_adx_joint_feed_status feed(void *context,struct kosdec_adx_joint *joint) {
    struct kosdec_adx_loader *loader=context;
    CHECK(kosdec_adx_loader_get_input_joint(loader)==joint);
    enum kosdec_adx_loader_status status=kosdec_adx_loader_step(loader);
    CHECK(status==KOSDEC_ADX_LOADER_OK || status==KOSDEC_ADX_LOADER_NEED_OUTPUT);
    struct kosdec_adx_loader_info info;
    CHECK(kosdec_adx_loader_get_info(loader,&info));
    return info.pending?KOSDEC_ADX_JOINT_FEED_MORE:KOSDEC_ADX_JOINT_FEED_END;
}
static void le32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
static void archive_pipeline(void) {
    size_t bytes;uint8_t *audio=stream(50,18,2,32,65,&bytes);
    memset(audio+20,0,24);
    uint8_t archive[512]={0};memcpy(archive,"AFS",3);
    le32(archive+4,1);le32(archive+8,32);le32(archive+12,(uint32_t)bytes);
    memcpy(archive+32,audio,bytes);
    char path[]="/tmp/kosdec-archive-XXXXXX";int fd=mkstemp(path);
    CHECK(fd>=0 && write(fd,archive,32+bytes)==(ssize_t)(32+bytes) && !close(fd));
    struct kosdec_adx_files files;struct kosdec_adx_files_entry slot;
    CHECK(!kosdec_adx_files_init(&files,&slot,1));
    uint64_t id;int cleanup;uint8_t cache[512];
    CHECK(!kosdec_adx_files_open(&files,path,cache,sizeof(cache),&id,&cleanup) && !cleanup);
    struct kosdec_adx_source loan={0};CHECK(!kosdec_adx_files_borrow(&files,id,&loan,1));
    struct kosdec_adx_partition partition;struct kosdec_adx_range range;
    CHECK(kosdec_adx_partition_init(&partition,loan.read_at,loan.context,loan.bytes,&range,1)==KOSDEC_ADX_MORE);
    enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned n=0;n<100 && status<KOSDEC_ADX_DONE;++n) status=kosdec_adx_partition_step(&partition);
    CHECK(status==KOSDEC_ADX_DONE);
    struct kosdec_adx_partition_registry registry;kosdec_adx_partition_registry_init(&registry);
    CHECK(kosdec_adx_partition_bind(&registry,9,&partition)==KOSDEC_ADX_PARTITION_OK);
    struct kosdec_adx_view view;
    CHECK(kosdec_adx_partition_resolve_view(&registry,9,0,&view)==KOSDEC_ADX_PARTITION_OK);
    CHECK(view.context==loan.context && view.bytes==bytes && view.offset==32);
    uint8_t ring[128];struct kosdec_adx_joint joint;
    CHECK(kosdec_adx_joint_ring_init(&joint,ring,sizeof(ring),0)==KOSDEC_ADX_JOINT_OK);
    struct kosdec_adx_loader loader;CHECK(kosdec_adx_loader_init(&loader,&joint)==KOSDEC_ADX_LOADER_OK);
    uint64_t source_id;
    CHECK(kosdec_adx_loader_enqueue(&loader,"archive member",kosdec_adx_view_read_at,&view,view.bytes,0,1,&source_id)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&loader)==KOSDEC_ADX_LOADER_OK);
    struct kosdec_adx_joint_reader reader;
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_set_feed(&reader,feed,&loader)==KOSDEC_ADX_DONE);
    unsigned frames=0;int16_t output[64];status=KOSDEC_ADX_MORE;
    for(unsigned n=0;n<1000 && status<KOSDEC_ADX_DONE;++n) {
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,output,32);
        status=r.status;frames+=(unsigned)r.frames;
    }
    CHECK(status==KOSDEC_ADX_DONE && frames==65);
    struct kosdec_adx_loader_source_info source_info;
    CHECK(kosdec_adx_loader_get_source(&loader,source_id,&source_info) && source_info.state==KOSDEC_ADX_LOADER_END);
    CHECK(source_info.published_bytes==bytes && !strcmp(source_info.name,"archive member"));
    CHECK(kosdec_adx_files_close(&files,id,&cleanup)<0 && errno==EBUSY);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_close(&loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_partition_registry_close(&registry)==KOSDEC_ADX_PARTITION_OK);
    CHECK(kosdec_adx_partition_close(&partition)==KOSDEC_ADX_PARTITION_OK);
    CHECK(!kosdec_adx_files_return(&files,id,&loan));
    CHECK(!kosdec_adx_files_close(&files,id,&cleanup) && !cleanup);
    CHECK(!kosdec_adx_files_finish(&files));CHECK(!unlink(path));free(audio);
}
int main(void) {
    size_t bytes;
    uint8_t *audio=stream(50,18,1,32,65,&bytes);
    memset(audio+20,0,24);
    char path[]="/tmp/kosdec-files-XXXXXX";
    int fd=mkstemp(path);CHECK(fd>=0);
    CHECK(write(fd,audio,bytes)==(ssize_t)bytes);CHECK(!close(fd));
    struct kosdec_adx_files files;
    struct kosdec_adx_files_entry entries[3];
    CHECK(!kosdec_adx_files_init(&files,entries,3));
    uint64_t ids[4]={0};int cleanup=0;
    uint8_t cache[512];
    for(unsigned i=0;i<3;++i) {
        CHECK(!kosdec_adx_files_open(&files,path,i==0?cache:NULL,i==0?sizeof(cache):0,&ids[i],&cleanup));
        CHECK(!cleanup && ids[i]==i+1);
        if(i==1) close_failure=last_fd;
    }
    CHECK(kosdec_adx_files_open(&files,path,NULL,0,&ids[3],&cleanup)<0 && errno==ENOSPC && opens==3);
    char name[256];memset(name,0xa5,sizeof(name));
    CHECK(kosdec_adx_files_get_name(&files,ids[0],name,1)<0 && errno==ERANGE && (unsigned char)name[0]==0xa5);
    CHECK(!kosdec_adx_files_get_name(&files,ids[0],name,sizeof(name)) && !strcmp(name,path));
    struct kosdec_adx_source file={0},selected={0};
    CHECK(!kosdec_adx_files_borrow(&files,ids[0],&file,0));
    struct kosdec_adx_source_handle source;
    CHECK(kosdec_adx_source_init(&source,file.read_at,file.context,file.bytes)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_borrow(&source,&selected,42)==KOSDEC_ADX_DONE);
    struct kosdec_adx_stream decoder;
    kosdec_adx_stream_init(&decoder,selected.read_at,selected.context,selected.bytes,false);
    unsigned frames=0;int16_t output[64];
    for(unsigned n=0;n<1000;++n) {
        struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&decoder,output,32);
        frames+=(unsigned)r.frames;
        if(r.status>=KOSDEC_ADX_DONE) { CHECK(r.status==KOSDEC_ADX_DONE);break; }
    }
    CHECK(frames==65);
    struct kosdec_adx_files_result result;
    CHECK(kosdec_adx_files_close_all(&files,&result)<0 && errno==EBUSY);
    CHECK(result.closed==6 && result.retained==1 && result.cleanup_failed==2 && closes==2);
    CHECK(kosdec_adx_files_finish(&files)<0 && errno==EBUSY);
    CHECK(kosdec_adx_files_get_name(&files,ids[1],name,sizeof(name))<0 && errno==ENOENT);
    CHECK(kosdec_adx_source_return(&source,&selected)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_DONE);
    CHECK(!kosdec_adx_files_return(&files,ids[0],&file));
    close_failure=-1;
    CHECK(!kosdec_adx_files_close_all(&files,&result));
    CHECK(result.closed==1 && !result.retained && closes==3);
    CHECK(!kosdec_adx_files_open(&files,path,NULL,0,&ids[3],&cleanup) && ids[3]==4);
    CHECK(kosdec_adx_files_close(&files,ids[0],&cleanup)<0 && errno==ENOENT);
    CHECK(!kosdec_adx_files_close(&files,ids[3],&cleanup) && closes==4);
    CHECK(!kosdec_adx_files_finish(&files));
    CHECK(kosdec_adx_files_close_all(&files,&result)<0 && errno==EINVAL);
    CHECK(!unlink(path));free(audio);
    archive_pipeline();
    printf("ADX_FILES checks=%u failures=0\n",checks);
    return 0;
}
