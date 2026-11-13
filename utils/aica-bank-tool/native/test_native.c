#include "abk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static abk_sample_t samples[ABK_MAX_SAMPLES];
static abk_zone_t zones[ABK_MAX_ZONES];
static abk_plan_t plan;

static void poison(void) {
    memset(samples,0xa5,sizeof(samples)); memset(zones,0xa5,sizeof(zones));
    memset(&plan,0xa5,sizeof(plan));
}
static void unchanged(const void *p, size_t n) {
    const unsigned char *b=p;
    while(n--) assert(*b++==0xa5);
}
static void outputs_unchanged(void) {
    unchanged(&plan,sizeof(plan)); unchanged(samples,sizeof(samples));
    unchanged(zones,sizeof(zones));
}
static void extra_tests(const uint8_t *p, uint32_t n) {
    abk_plan_t valid=plan;
    abk_placement_t placement;
    uint32_t i;
    const uint32_t badbase[]={0,0x2ffe0,0x30001,0x200000,UINT32_MAX};
    poison();
    assert(abk_plan_build(p,n,&plan,samples,valid.sample_count-1,zones,ABK_MAX_ZONES)==ABK_CAPACITY);
    outputs_unchanged();
    assert(abk_plan_build(p,n,&plan,samples,ABK_MAX_SAMPLES,zones,valid.zone_count-1)==ABK_CAPACITY);
    outputs_unchanged();
    assert(abk_plan_build(p,n,(abk_plan_t *)samples,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ALIAS);
    outputs_unchanged();
    assert(abk_plan_build(p,n,&plan,samples,ABK_MAX_SAMPLES,(abk_zone_t *)samples,ABK_MAX_ZONES)==ABK_ALIAS);
    outputs_unchanged();
    assert(abk_plan_build(p,n,(abk_plan_t *)((uint8_t *)&plan+1),samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ARGUMENT);
    outputs_unchanged();
    assert(abk_plan_build(p,UINT32_MAX,&plan,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)!=ABK_OK);
    outputs_unchanged();
    assert(abk_plan_build(p,n,&plan,(abk_sample_t *)(UINTPTR_MAX-3),ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ARGUMENT);
    outputs_unchanged();
    if((uintptr_t)p % _Alignof(abk_plan_t)==0)
        assert(abk_plan_build(p,n,(abk_plan_t *)p,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ALIAS);
    for(i=0;i<sizeof(badbase)/sizeof(*badbase);++i) {
        memset(&placement,0xa5,sizeof(placement));
        assert(abk_plan_place(&valid,badbase[i],valid.payload_bytes,&placement)==ABK_DESTINATION);
        unchanged(&placement,sizeof(placement));
    }
    memset(&placement,0xa5,sizeof(placement));
    assert(abk_plan_place(&valid,0x30000,valid.payload_bytes-1,&placement)==ABK_DESTINATION);
    assert(abk_plan_place(&valid,0x30000,UINT32_MAX,&placement)==ABK_DESTINATION);
    unchanged(&placement,sizeof(placement));
    assert(abk_plan_place(&valid,0x200000-valid.payload_bytes,valid.payload_bytes,&placement)==ABK_OK);
    assert(placement.bytes==valid.payload_bytes && placement.source_file_offset==valid.payload_file_offset);
    assert(placement.sound_ram_offset+placement.bytes==0x200000);
    {
        abk_placement_t chunk;
        uint32_t cursor=0;
        memset(&chunk,0xa5,sizeof(chunk));
        assert(abk_plan_chunk(&placement,0,31,&chunk)==ABK_DESTINATION);
        assert(abk_plan_chunk(&placement,1,32,&chunk)==ABK_DESTINATION);
        assert(abk_plan_chunk(&placement,UINT32_MAX,32,&chunk)==ABK_DESTINATION);
        assert(abk_plan_chunk(&placement,0,32800,&chunk)==ABK_DESTINATION);
        assert(abk_plan_chunk(&placement,0,0,&chunk)==ABK_DESTINATION);
        unchanged(&chunk,sizeof(chunk));
        assert(abk_plan_chunk(&placement,0,32,&placement)==ABK_ALIAS);
        do {
            assert(abk_plan_chunk(&placement,cursor,32768,&chunk)==ABK_OK);
            assert(chunk.source_file_offset==valid.payload_file_offset+cursor);
            assert(chunk.sound_ram_offset==placement.sound_ram_offset+cursor);
            assert(chunk.bytes<=32768 && !(chunk.bytes&31u));
            assert(chunk.bytes || cursor==valid.payload_bytes);
            cursor+=chunk.bytes;
        } while(chunk.bytes);
        assert(cursor==valid.payload_bytes);
        assert(abk_plan_chunk(&placement,0,32,&chunk)==ABK_OK && chunk.bytes==32);
        placement.source_file_offset=UINT32_MAX;
        memset(&chunk,0xa5,sizeof(chunk));
        assert(abk_plan_chunk(&placement,0,32,&chunk)==ABK_DESTINATION);
        unchanged(&chunk,sizeof(chunk));
    }
    assert(abk_plan_place(&valid,0x30000,valid.payload_bytes,(abk_placement_t *)&valid)==ABK_ALIAS);
    assert(abk_plan_build(p,n,&plan,samples,UINT32_MAX,zones,UINT32_MAX)==ABK_OK);
}

int main(void) {
    uint8_t header[4];
    poison();
    assert(abk_plan_build(NULL,0,&plan,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ARGUMENT);
    assert(abk_plan_build((void *)(UINTPTR_MAX-2),4,&plan,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES)==ABK_ARGUMENT);
    outputs_unchanged();
    while(fread(header,1,4,stdin)==4) {
        uint32_t n=(uint32_t)header[0]|(uint32_t)header[1]<<8|(uint32_t)header[2]<<16|(uint32_t)header[3]<<24;
        uint32_t alignment,i,j;
        int first=-1;
        uint8_t *original;
        assert(n<=ABK_MAX_FILE+1);
        original=malloc(n ? n : 1); assert(original);
        assert(fread(original,1,n,stdin)==n);
        for(alignment=0;alignment<4;++alignment) {
            uint8_t *allocation=malloc(n+alignment+(!n));
            uint8_t *data=allocation+alignment;
            abk_result_t result;
            assert(allocation); memcpy(data,original,n); poison();
            result=abk_plan_build(data,n,&plan,samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES);
            if(first<0) first=(int)result;
            assert(first==(int)result);
            if(result!=ABK_OK) outputs_unchanged();
            else {
                unchanged(samples+plan.sample_count,sizeof(samples)-plan.sample_count*sizeof(*samples));
                unchanged(zones+plan.zone_count,sizeof(zones)-plan.zone_count*sizeof(*zones));
                extra_tests(data,n);
            }
            assert(memcmp(data,original,n)==0);
            free(allocation);
        }
        printf("%d",first);
        if(first==ABK_OK) {
            printf(" %u %u %u %u %u",plan.file_bytes,plan.payload_file_offset,
                   plan.payload_bytes,plan.sample_count,plan.zone_count);
            for(i=0;i<plan.sample_count;++i) {
                abk_sample_t *s=&samples[i];
                printf(" %u %u %u %u %u %u %u %u %u",s->file_offset,s->payload_offset,
                       s->bytes,s->frames,s->rate,s->loop_start,s->loop_end,s->encoding,s->loop);
            }
            for(i=0;i<plan.zone_count;++i) {
                printf(" %u",zones[i].sample);
                for(j=0;j<20;++j) printf(" %d",zones[i].values[j]);
            }
        }
        puts(""); free(original);
    }
    assert(!ferror(stdin));
    return 0;
}
