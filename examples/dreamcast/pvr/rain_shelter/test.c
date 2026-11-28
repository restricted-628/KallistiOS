#undef NDEBUG
#include "rain.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    pvr_particle_t covered[RAIN_DROPS], reference[RAIN_DROPS], replay[RAIN_DROPS];
    rain_start(covered);
    rain_visibility(covered,&roof);
    memcpy(replay,covered,sizeof(covered));
    memcpy(reference,covered,sizeof(covered));
    rain_visibility(reference,NULL);
    assert(!(covered[0].flags & PVR_PARTICLE_HIDDEN) && (covered[1].flags & PVR_PARTICLE_HIDDEN));
    double x=-2, vx=2, age=0;
    const int hidden[] = {0,1,1,0,0};
    for(size_t i=0; i<sizeof(gusts)/sizeof(*gusts); ++i) {
        rain_tick_t tick=gusts[i];
        x += vx*tick.dt + .5*tick.wind_x*tick.dt*tick.dt;
        vx += tick.wind_x*tick.dt;
        age += tick.dt;
        assert(rain_step(covered,&roof,tick)==0);
        assert(rain_step(reference,NULL,tick)==0);
        assert(rain_step(replay,&roof,tick)==0);
        assert(memcmp(replay,covered,sizeof(covered))==0);
        assert(covered[0].position.x == x && covered[0].position.y == -1+.75*age);
        assert(covered[0].age == age && covered[0].velocity.x == vx);
        assert(!!(covered[0].flags & PVR_PARTICLE_HIDDEN) == hidden[i]);
        assert(covered[0].position.x==reference[0].position.x &&
               covered[0].velocity.x==reference[0].velocity.x && covered[0].age==reference[0].age);
        assert(covered[0].flags & PVR_PARTICLE_ACTIVE);
        assert(!!(covered[1].flags & PVR_PARTICLE_ACTIVE) == (age < .5));
        if(age>=.5)
            assert(covered[1].age==.5f);
    }
    pvr_particle_t before[RAIN_DROPS];
    memcpy(before,covered,sizeof(before));
    assert(rain_step(covered,&roof,(rain_tick_t){-1,3})<0);
    assert(memcmp(covered,before,sizeof(before))==0);
    covered[0].position=roof.min;
    rain_visibility(covered,&roof);
    assert(covered[0].flags & PVR_PARTICLE_HIDDEN);
    covered[0].position.x=roof.max.x;
    rain_visibility(covered,&roof);
    assert(!(covered[0].flags & PVR_PARTICLE_HIDDEN));
    puts("SHELTERED RAIN REPLAY PASS");
    return 0;
}
