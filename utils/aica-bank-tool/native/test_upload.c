#include "abk_upload.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct spu_transfer_request { int live; };
static struct spu_transfer_request req;
static spu_transfer_status_t status;
static unsigned char ram[0x200000];
static const unsigned char *source;
static uintptr_t destination;
static uint32_t allocation, expected_offset;
static size_t allocation_bytes;
static int in_irq, alloc_error, free_error, submit_error, status_error;
static int destroy_error, cancel_error, allocs, frees, submits, destroys;
static int queries, cancels, checks;
static abk_sample_t samples[ABK_MAX_SAMPLES];
static abk_zone_t zones[ABK_MAX_ZONES];
#define CHECK(x) do { ++checks; assert(x); } while(0)

int irq_inside_int(void) { return in_irq; }
uint32_t snd_mem_malloc(size_t size) {
    ++allocs;
    CHECK(!allocation_bytes);
    if(alloc_error) { errno = alloc_error; return 0; }
    allocation_bytes = size;
    return allocation;
}
void snd_mem_free(uint32_t address) {
    ++frees;
    CHECK(address == allocation && allocation_bytes && !req.live);
    if(free_error) { errno = free_error; return; }
    allocation_bytes = 0;
}
int spu_memload_async(uintptr_t to, const void *from, size_t n,
                     spu_transfer_method_t method, uint32_t lifetime,
                     spu_transfer_callback_t callback, void *arg,
                     spu_transfer_request_t **out) {
    ++submits;
    CHECK(!req.live && allocation_bytes);
    CHECK(to == allocation + expected_offset && n && n <= 32768 && !(n & 31));
    CHECK(to + n <= allocation + allocation_bytes);
    CHECK(method == SPU_TRANSFER_AUTO && lifetime == 17 && !callback && !arg);
    *out = NULL;
    if(submit_error) { errno = submit_error; return -1; }
    req.live = 1; *out = &req; source = from; destination = to;
    status = (spu_transfer_status_t){
        .state = SPU_TRANSFER_QUEUED, .direction = SPU_TRANSFER_TO_SOUND_RAM,
        .requested_method = method,
        .active_method = ((uintptr_t)from & 31) ? SPU_TRANSFER_PIO : SPU_TRANSFER_DMA,
        .requested_bytes = n, .result = EINPROGRESS
    };
    return 0;
}
int spu_transfer_get_status(const spu_transfer_request_t *r,
                            spu_transfer_status_t *out) {
    ++queries; CHECK(r == &req && req.live);
    if(status_error) { errno = status_error; return -1; }
    *out = status; return 0;
}
int spu_transfer_cancel(spu_transfer_request_t *r) {
    ++cancels; CHECK(r == &req && req.live);
    if(cancel_error) { errno = cancel_error; return -1; }
    if(status.state >= SPU_TRANSFER_COMPLETE) { errno = EALREADY; return -1; }
    /* Intentionally asynchronous: cancellation does not prove retirement. */
    return 0;
}
int spu_transfer_destroy(spu_transfer_request_t *r) {
    ++destroys; CHECK(r == &req && req.live);
    CHECK(status.state >= SPU_TRANSFER_COMPLETE && status.state <= SPU_TRANSFER_ERROR);
    if(destroy_error) { errno = destroy_error; return -1; }
    req.live = 0;
    if(status.state == SPU_TRANSFER_COMPLETE && status.result == 0)
        expected_offset += (uint32_t)status.requested_bytes;
    return 0;
}
static void reset(void) {
    CHECK(!req.live && !allocation_bytes);
    in_irq = alloc_error = free_error = submit_error = status_error = 0;
    destroy_error = cancel_error = allocs = frees = submits = destroys = 0;
    queries = cancels = 0; expected_offset = 0; allocation = 0x30000;
    memset(ram, 0xa5, sizeof(ram));
}
static void complete(void) {
    CHECK(req.live);
    memcpy(ram + destination, source, status.requested_bytes);
    status.state = SPU_TRANSFER_COMPLETE;
    status.completed_bytes = status.requested_bytes; status.result = 0;
}
static int begin(abk_upload_t *t, const void *b, uint32_t n, uint32_t limit) {
    return abk_upload_begin(t,b,n,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES,
                            limit,17);
}
static void closed(abk_upload_t *t) {
    int calls;
    CHECK(abk_upload_close(t) == 0 && t->state == ABKU_CLOSED && !t->address);
    calls = allocs + frees + submits + destroys + queries + cancels;
    CHECK(abk_upload_close(t) == 0);
    CHECK(abk_upload_pump(t) == 0);
    CHECK(abk_upload_cancel(t) == 0);
    CHECK(begin(t,"x",1,32) == -1 && errno == EALREADY);
    CHECK(calls == allocs + frees + submits + destroys + queries + cancels);
}
static void cancel_finish(abk_upload_t *t) {
    CHECK(abk_upload_cancel(t) == 0);
    CHECK(req.live && allocation_bytes);
    CHECK(abk_upload_pump(t) == 0 && t->state != ABKU_READY);
    status.state = SPU_TRANSFER_CANCELLED; status.result = ECANCELED;
    CHECK(abk_upload_pump(t) == -1 && t->state == ABKU_CANCELLED && !req.live);
    closed(t);
}

static void run(const unsigned char *b, uint32_t n) {
    abk_upload_t t;
    int i;
    /* Actual chunk coverage, source byte alignment and pending-state pumps. */
    for(i=0;i<4;++i) {
        reset(); t=(abk_upload_t){0};
        CHECK(begin(&t,b,n,(uint32_t[]){32,64,32736,32768}[i])==0);
        CHECK(begin(&t,b,n,32)==-1 && errno==EALREADY && submits==1);
        CHECK(abk_upload_close(&t)==-1 && errno==EBUSY && !frees);
        while(t.state!=ABKU_READY) {
            int q=queries;
            CHECK(abk_upload_pump(&t)==0 && queries==q+1);
            status.state=SPU_TRANSFER_RUNNING;
            CHECK(abk_upload_pump(&t)==0);
            CHECK(source==b+t.plan.payload_file_offset+t.committed);
            complete(); CHECK(abk_upload_pump(&t)==0);
        }
        CHECK(t.committed==t.plan.payload_bytes && !req.live && !frees);
        CHECK(!memcmp(ram+t.address,b+t.plan.payload_file_offset,t.plan.payload_bytes));
        CHECK(t.first_error==0);closed(&t);
    }
    reset();t=(abk_upload_t){0};alloc_error=ENOMEM;
    CHECK(begin(&t,b,n,32768)==-1 && t.first_error==ENOMEM && !submits);closed(&t);
    reset();t=(abk_upload_t){0};allocation=0x200000;
    CHECK(begin(&t,b,n,32768)==-1 && t.first_error==ERANGE && frees==1 && !submits);closed(&t);
    reset();t=(abk_upload_t){0};allocation=0x200000;free_error=EAGAIN;
    CHECK(begin(&t,b,n,32768)==-1 && t.first_error==ERANGE && t.last_error==EAGAIN);
    CHECK(t.address && allocation_bytes);free_error=0;closed(&t);
    reset();t=(abk_upload_t){0};submit_error=ENOMEM;
    CHECK(begin(&t,b,n,32768)==-1 && t.first_error==ENOMEM && t.address && !req.live);closed(&t);
    reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32)==0);complete();submit_error=EAGAIN;
    CHECK(abk_upload_pump(&t)==-1 && t.first_error==EAGAIN && t.committed==32);
    CHECK(!req.live && submits==2);closed(&t);
    /* Failed/unknown status cannot retire or resume admission after recovery. */
    for(i=0;i<2;++i) {
        reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);
        if(i) status.state=(spu_transfer_state_t)99;else status_error=EFAULT;
        CHECK(abk_upload_pump(&t)==-1 && req.live && !destroys && !frees && !t.committed);
        CHECK(t.first_error==(i?EIO:EFAULT));status_error=0;complete();
        CHECK(abk_upload_pump(&t)==-1 && t.state==ABKU_FAILED && submits==1);closed(&t);
    }
    /* Every terminal failure and malformed successful result stays unpublished. */
    for(i=0;i<7;++i) {
        reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);complete();
        switch(i) {
            case 0:status.state=SPU_TRANSFER_CANCELLED;status.result=ECANCELED;break;
            case 1:status.state=SPU_TRANSFER_TIMED_OUT;status.result=ETIMEDOUT;break;
            case 2:status.state=SPU_TRANSFER_ERROR;status.result=ENODEV;break;
            case 3:--status.completed_bytes;break;
            case 4:++status.requested_bytes;break;
            case 5:status.direction=SPU_TRANSFER_FROM_SOUND_RAM;break;
            case 6:status.result=EIO;break;
        }
        CHECK(abk_upload_pump(&t)==-1 && t.first_error && !t.committed && submits==1);
        CHECK(t.state==ABKU_FAILED && !req.live);closed(&t);
    }
    /* Final COMPLETE + destroy EBUSY, both resumed success and late cancellation. */
    for(i=0;i<2;++i) {
        reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);
        while(t.committed+t.chunk_bytes<t.placement.bytes) {complete();CHECK(abk_upload_pump(&t)==0);}
        complete();destroy_error=EBUSY;
        CHECK(abk_upload_pump(&t)==-1 && errno==EBUSY && !t.first_error);
        CHECK(req.live && t.state!=ABKU_READY && !frees);
        if(i) CHECK(abk_upload_cancel(&t)==0 && t.cancelled);
        destroy_error=0;
        CHECK(abk_upload_pump(&t)==(i?-1:0));
        CHECK(t.state==(i?ABKU_CANCELLED:ABKU_READY) && !req.live);closed(&t);
    }
    reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);complete();destroy_error=EFAULT;
    CHECK(abk_upload_pump(&t)==-1 && t.first_error==EFAULT && req.live);
    destroy_error=EBUSY;CHECK(abk_upload_pump(&t)==-1 && t.first_error==EFAULT && t.last_error==EBUSY);
    destroy_error=0;CHECK(abk_upload_pump(&t)==-1 && t.state==ABKU_FAILED);closed(&t);
    reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);cancel_finish(&t);
    reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);status.state=SPU_TRANSFER_RUNNING;
    cancel_error=EIO;CHECK(abk_upload_cancel(&t)==-1 && t.first_error==EIO && t.cancelled);
    cancel_error=0;complete();CHECK(abk_upload_pump(&t)==-1 && t.state==ABKU_CANCELLED);
    free_error=EAGAIN;CHECK(abk_upload_close(&t)==-1 && t.address && t.first_error==EIO);
    free_error=0;closed(&t);
    reset();t=(abk_upload_t){0};CHECK(begin(&t,b,n,32768)==0);
    in_irq=1;CHECK(abk_upload_pump(&t)==-1 && errno==EPERM && !queries);
    CHECK(abk_upload_close(&t)==-1 && !frees);in_irq=0;cancel_finish(&t);
    reset();t=(abk_upload_t){0};
    CHECK(begin(&t,b,n,0)==-1 && !allocs);
    CHECK(begin(&t,b,n,33)==-1 && !allocs);
    CHECK(begin(&t,b,n,32769)==-1 && !allocs);
    CHECK(abk_upload_begin(&t,b,n,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES,32,0)==-1 && !allocs);
    CHECK(begin(&t,&t,(uint32_t)sizeof(t),32)==-1 && !allocs);
    CHECK(abk_upload_begin(&t,b,n,(abk_sample_t *)&t,1,zones,ABK_MAX_ZONES,32,17)==-1 && !allocs);
    CHECK(begin(&t,b,n-1,32)==-1 && t.validation!=ABK_OK && !allocs);closed(&t);
    /* R1: nonempty state must not bypass immutable alias rejection. */
    for(i=ABKU_UPLOADING;i<=ABKU_CLOSED;++i) {
        abk_upload_t saved;
        reset();t=(abk_upload_t){.state=(abk_upload_state_t)i};saved=t;
        CHECK(begin(&t,&t,(uint32_t)sizeof(t),32)==-1 && errno==EINVAL);
        CHECK(!memcmp(&t,&saved,sizeof(t)));
        CHECK(abk_upload_begin(&t,b,n,(abk_sample_t *)&t,1,zones,ABK_MAX_ZONES,
                               32,17)==-1 && errno==EINVAL);
        CHECK(!memcmp(&t,&saved,sizeof(t)));
        CHECK(abk_upload_begin(&t,b,n,samples,ABK_MAX_SAMPLES,(abk_zone_t *)&t,1,
                               32,17)==-1 && errno==EINVAL);
        CHECK(!memcmp(&t,&saved,sizeof(t)));
        CHECK(!(allocs+frees+submits+destroys+queries+cancels));
    }
}
int main(int argc,char **argv) {
    FILE *f;long length;unsigned char *storage;int offset;
    CHECK(argc==2);f=fopen(argv[1],"rb");CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));length=ftell(f);CHECK(length>0 && length<0x200000);
    rewind(f);storage=aligned_alloc(32,((size_t)length+63)&~(size_t)31);CHECK(storage);
    CHECK(fread(storage+32,1,(size_t)length,f)==(size_t)length);CHECK(!fclose(f));
    for(offset=0;offset<4;++offset) {
        memmove(storage+offset,storage+(offset?offset-1:32),(size_t)length);
        run(storage+offset,(uint32_t)length);
    }
    free(storage);printf("Private bank upload: %d checks PASS (4 source alignments)\n",checks);
    return 0;
}
