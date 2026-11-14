#define SH4DER_CORE_NO_SH4ZAM
#include <sh4der_kos_ao.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static uint16_t grid[4800],map[8192],saved[8192];
static uint8_t luma[4800],old_luma[4800];
static float reference[4800];
int main(void) {
    for(unsigned i=0;i<4800;++i)grid[i]=(uint16_t)(i*7919u);
    memset(map,0xa5,sizeof(map));memcpy(saved,map,sizeof(map));
    memset(luma,0x5a,sizeof(luma));memcpy(old_luma,luma,sizeof(luma));
    assert(sh4der_kos_ao_map(grid,4799,luma,4800,map,8192)==-1 && errno==ENOSPC);
    assert(sh4der_kos_ao_map(grid,4800,luma,4799,map,8192)==-1);
    assert(sh4der_kos_ao_map(grid,4800,luma,4800,map,8191)==-1);
    assert(!memcmp(map,saved,sizeof(map)) && !memcmp(luma,old_luma,sizeof(luma)));
    assert(sh4der_kos_ao_map(grid,4800,luma,4800,map,8192)==0);
    sh4der_ao_grid(luma,80,60,reference);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x)assert(map[y*128+x]==(y<60 && x<80?sh4der_ao_texel565(reference[y*80+x]):0xffff));
    for(unsigned i=0;i<4800;++i)grid[i]=i<2400?0:0xffff;
    assert(sh4der_kos_ao_map(grid,4800,luma,4800,map,8192)==0);
    assert(map[30*128+40]==0x8410 && map[29*128+40]==0xffff && map[31*128+40]==0xffff);
    assert(map[30*128]==0xffff && map[30*128+79]==0xffff);
    puts("SH4DER cavity host PASS");return 0;
}
