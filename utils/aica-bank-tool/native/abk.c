#include "abk.h"
#include <stddef.h>

_Static_assert(sizeof(abk_plan_t) == 20, "plan layout");
_Static_assert(sizeof(abk_sample_t) == 36, "sample layout");
_Static_assert(sizeof(abk_zone_t) == 84, "zone layout");

/* Explicit byte loads; neither SH4 nor ARM7DI permits general unaligned words. */
static uint32_t u16(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t s16(const uint8_t *p) {
    uint32_t v = u16(p);
    return (int32_t)v - (v & 0x8000u ? 65536 : 0);
}
static int zero(const uint8_t *p, uint32_t n) {
    while(n--) if(*p++) return 0;
    return 1;
}
static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = UINT32_MAX, i, bit;
    for(i = 0; i < n; ++i) {
        crc ^= i >= 40 && i < 44 ? 0 : p[i];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static int span(const void *p, uintptr_t n) {
    return p && n <= UINTPTR_MAX - (uintptr_t)p;
}
static int overlap(const void *a, uintptr_t an, const void *b, uintptr_t bn) {
    return (uintptr_t)a < (uintptr_t)b + bn &&
           (uintptr_t)b < (uintptr_t)a + an;
}
static int aligned(const void *p, uintptr_t a) {
    return (uintptr_t)p % a == 0;
}

abk_result_t abk_plan_build(const void *data, uint32_t size, abk_plan_t *plan,
                          abk_sample_t *samples, uint32_t sample_capacity,
                          abk_zone_t *zones, uint32_t zone_capacity) {
    const uint8_t *p = data;
    uint32_t ns, nz, zo, po, pb, cursor, i, j;
    uintptr_t sb, zb;
    static const uint8_t magic[8] = {'K','O','S','A','B','K',0,0};
    if(!span(data, size) || !plan || !samples || !zones ||
       !aligned(plan, _Alignof(abk_plan_t)) ||
       !aligned(samples, _Alignof(abk_sample_t)) ||
       !aligned(zones, _Alignof(abk_zone_t))) return ABK_ARGUMENT;
    if(size < 64 || size > ABK_MAX_FILE) return ABK_FORMAT;
    for(i = 0; i < 8; ++i) if(p[i] != magic[i]) return ABK_FORMAT;
    if(u16(p+8) != 1 || u16(p+10) != 64 || u32(p+12) != size ||
       u32(p+44) || !zero(p+48,16)) return ABK_FORMAT;
    ns = u32(p+16); nz = u32(p+20);
    if(!ns || ns > ABK_MAX_SAMPLES || !nz || nz > ABK_MAX_ZONES)
        return ABK_FORMAT;
    /* Counts are bounded before multiply/add. Maximum table end is41024. */
    zo = 64 + ns * 32; po = zo + nz * 32;
    pb = u32(p+36);
    if(u32(p+24) != 64 || u32(p+28) != zo || u32(p+32) != po ||
       po > size || !pb || pb > ABK_MAX_PAYLOAD || pb != size-po)
        return ABK_FORMAT;
    if(crc32(p,size) != u32(p+40)) return ABK_CRC;
    cursor = po;
    for(i = 0; i < ns; ++i) {
        const uint8_t *s = p+64+i*32;
        uint32_t off=u32(s), bytes=u32(s+4), frames=u32(s+8), rate=u32(s+12);
        uint32_t ls=u32(s+16), le=u32(s+20), enc=u16(s+24), flags=u16(s+26);
        if((enc != 1 && enc != 2) || flags > 1 || u32(s+28) ||
           !frames || frames > 65534 || bytes != frames*enc ||
           !rate || rate > 192000) return ABK_FORMAT;
        if((!flags && (ls || le)) || (flags && (ls >= le || le > frames)))
            return ABK_FORMAT;
        if(off != cursor || bytes > size-cursor) return ABK_FORMAT;
        cursor += bytes; /* <=size<=2MiB, including subsequent +31 */
        j = (cursor+31u) & ~31u;
        if(j > size || !zero(p+cursor,j-cursor)) return ABK_FORMAT;
        cursor = j;
    }
    if(cursor != size) return ABK_FORMAT;
    for(i = 0; i < nz; ++i) {
        const uint8_t *z = p+zo+i*32;
        if(u32(z) >= ns || !zero(z+25,7) || z[4]>127 || z[5]>127 || z[6]>1 ||
           z[7]>z[8] || z[8]>127 || !z[9] || z[9]>z[10] || z[10]>127 ||
           z[11]>127 || s16(z+12)<-100 || s16(z+12)>100) return ABK_FORMAT;
        for(j=16; j<=20; ++j) if(z[j]>31) return ABK_FORMAT;
        for(j=21; j<=24; ++j) if(z[j]>15) return ABK_FORMAT;
        for(j=0; j<i; ++j) {
            const uint8_t *q = p+zo+j*32;
            if(z[4]==q[4] && z[5]==q[5] && z[6]==q[6] &&
               z[8]>=q[7] && q[8]>=z[7] && z[10]>=q[9] && q[10]>=z[9])
                return ABK_FORMAT;
        }
    }
    if(sample_capacity < ns || zone_capacity < nz) return ABK_CAPACITY;
    sb = ns*sizeof(*samples); zb = nz*sizeof(*zones);
    if(!span(plan,sizeof(*plan)) || !span(samples,sb) || !span(zones,zb))
        return ABK_ARGUMENT;
    if(overlap(data,size,plan,sizeof(*plan)) || overlap(data,size,samples,sb) ||
       overlap(data,size,zones,zb) || overlap(plan,sizeof(*plan),samples,sb) ||
       overlap(plan,sizeof(*plan),zones,zb) || overlap(samples,sb,zones,zb))
        return ABK_ALIAS;
    /* No failure path beyond here; input is immutable and outputs disjoint. */
    for(i=0; i<ns; ++i) {
        const uint8_t *s=p+64+i*32;
        samples[i].file_offset=u32(s); samples[i].payload_offset=u32(s)-po;
        samples[i].bytes=u32(s+4); samples[i].frames=u32(s+8);
        samples[i].rate=u32(s+12); samples[i].loop_start=u32(s+16);
        samples[i].loop_end=u32(s+20); samples[i].encoding=u16(s+24);
        samples[i].loop=u16(s+26);
    }
    for(i=0; i<nz; ++i) {
        const uint8_t *z=p+zo+i*32;
        zones[i].sample=u32(z);
        for(j=0; j<8; ++j) zones[i].values[j]=z[4+j];
        zones[i].values[8]=s16(z+12);
        for(j=9; j<20; ++j) zones[i].values[j]=z[5+j];
    }
    plan->file_bytes=size; plan->payload_file_offset=po; plan->payload_bytes=pb;
    plan->sample_count=ns; plan->zone_count=nz;
    return ABK_OK;
}

abk_result_t abk_plan_place(const abk_plan_t *plan, uint32_t base,
                          uint32_t reserved_bytes, abk_placement_t *out) {
    if(!span(plan,sizeof(*plan)) || !span(out,sizeof(*out)) ||
       !aligned(plan,_Alignof(abk_plan_t)) || !aligned(out,_Alignof(abk_placement_t)))
        return ABK_ARGUMENT;
    if(overlap(plan,sizeof(*plan),out,sizeof(*out))) return ABK_ALIAS;
    if(!plan->payload_bytes || plan->payload_bytes > ABK_MAX_PAYLOAD ||
       (plan->payload_bytes & 31u) || base < 0x30000u || base >= 0x200000u ||
       (base & 31u) || reserved_bytes > 0x200000u-base ||
       plan->payload_bytes > reserved_bytes) return ABK_DESTINATION;
    out->source_file_offset=plan->payload_file_offset;
    out->sound_ram_offset=base;
    out->bytes=plan->payload_bytes;
    return ABK_OK;
}

abk_result_t abk_plan_chunk(const abk_placement_t *placement,
                          uint32_t committed, uint32_t limit,
                          abk_placement_t *out) {
    uint32_t count;
    if(!span(placement,sizeof(*placement)) || !span(out,sizeof(*out)) ||
       !aligned(placement,_Alignof(abk_placement_t)) ||
       !aligned(out,_Alignof(abk_placement_t))) return ABK_ARGUMENT;
    if(overlap(placement,sizeof(*placement),out,sizeof(*out))) return ABK_ALIAS;
    if(!limit || limit>32768 || (limit&31u) || (committed&31u) ||
       !placement->bytes || placement->bytes>ABK_MAX_PAYLOAD ||
       (placement->bytes&31u) || committed>placement->bytes ||
       (placement->source_file_offset&31u) ||
       placement->source_file_offset>ABK_MAX_FILE ||
       placement->bytes>ABK_MAX_FILE-placement->source_file_offset ||
       placement->sound_ram_offset<0x30000u ||
       placement->sound_ram_offset>=0x200000u ||
       (placement->sound_ram_offset&31u) ||
       placement->bytes>0x200000u-placement->sound_ram_offset)
        return ABK_DESTINATION;
    count=placement->bytes-committed;
    if(count>limit) count=limit;
    out->source_file_offset=placement->source_file_offset+committed;
    out->sound_ram_offset=placement->sound_ram_offset+committed;
    out->bytes=count;
    return ABK_OK;
}
