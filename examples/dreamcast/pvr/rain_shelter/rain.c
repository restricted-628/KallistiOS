#include "rain.h"

const rain_shelter_t roof = {.min={-1,-2,1,0}, .max={.75f,2,3,0}};
const rain_tick_t gusts[] = {{.25f,1},{.375f,-1},{.125f,.5f},{.625f,.25f},{.25f,-.5f}};

void rain_start(pvr_particle_t drops[RAIN_DROPS]) {
    const pvr_particle_t moving = {.position={-2,-1,2,0}, .velocity={2,.75f,0,0},
        .flags=PVR_PARTICLE_ACTIVE,.lifetime=5,.scale_x=1,.scale_y=1,.color=0xffffffff};
    drops[0] = moving;
    drops[1] = moving;
    drops[1].position.x = 0;
    drops[1].position.y = .875f;
    drops[1].velocity.x = 0;
    drops[1].lifetime = .5f;
    drops[2] = moving;
    drops[2].position.x = 2;
    drops[2].position.y = 1.5f;
    drops[2].velocity.x = 0;
    drops[2].velocity.y = .25f;
}

void rain_visibility(pvr_particle_t drops[RAIN_DROPS], const rain_shelter_t *shelter) {
    for(unsigned i=0; i<RAIN_DROPS; ++i) {
        if(!(drops[i].flags & PVR_PARTICLE_ACTIVE))
            continue;
        const point_t p = drops[i].position;
        if(shelter && p.x >= shelter->min.x && p.x < shelter->max.x &&
           p.y >= shelter->min.y && p.y < shelter->max.y &&
           p.z >= shelter->min.z && p.z < shelter->max.z)
            drops[i].flags |= PVR_PARTICLE_HIDDEN;
        else
            drops[i].flags &= ~PVR_PARTICLE_HIDDEN;
    }
}

int rain_step(pvr_particle_t drops[RAIN_DROPS], const rain_shelter_t *shelter, rain_tick_t tick) {
    const pvr_particle_stream_t stream = {drops, RAIN_DROPS, sizeof(*drops)};
    float previous_wind[RAIN_DROPS];
    for(unsigned i=0; i<RAIN_DROPS; ++i) {
        previous_wind[i] = drops[i].acceleration.x;
        if(drops[i].flags & PVR_PARTICLE_ACTIVE)
            drops[i].acceleration.x = tick.wind_x;
    }
    if(pvr_particle_step(&stream, tick.dt, NULL) < 0) {
        for(unsigned i=0; i<RAIN_DROPS; ++i)
            drops[i].acceleration.x = previous_wind[i];
        return -1;
    }
    rain_visibility(drops, shelter);
    return 0;
}
