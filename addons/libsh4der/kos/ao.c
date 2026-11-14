/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_ao.h>
#include <sh4der/sh4der_glow.h>
#include <errno.h>
#include <string.h>
int sh4der_kos_ao_map(const uint16_t *grid,size_t count,uint8_t *luma,size_t scratch_count,
    uint16_t *out,size_t capacity) {
    if(!grid || !luma || !out){errno=EINVAL;return -1;}
    if(count<4800 || scratch_count<4800 || capacity<8192){errno=ENOSPC;return -1;}
    for(unsigned i=0;i<4800;++i)luma[i]=(uint8_t)sh4der_glow_luma8_(grid[i]);
    memset(out,0xff,8192*sizeof(*out));
    for(unsigned y=1;y<60;++y)for(unsigned x=1;x<79;++x) {
        const uint8_t *above=luma+(y-1)*80;
        float mean=(above[x-1]+2u*above[x]+above[x+1])*.25f;
        out[y*128+x]=sh4der_ao_texel565(sh4der_ao_screen(luma[y*80+x],mean));
    }
    return 0;
}
