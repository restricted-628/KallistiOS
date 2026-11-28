#ifndef EXAMPLE_RAIN_SHELTER_H
#define EXAMPLE_RAIN_SHELTER_H
#include <dc/pvr_particle.h>

enum { RAIN_DROPS = 3 };
typedef struct rain_shelter {
    point_t min, max; /* World-space prism, lower inclusive and upper exclusive. */
} rain_shelter_t;
typedef struct rain_tick {
    float dt, wind_x;
} rain_tick_t;

extern const rain_shelter_t roof;
extern const rain_tick_t gusts[5];
void rain_start(pvr_particle_t drops[RAIN_DROPS]);
int rain_step(pvr_particle_t drops[RAIN_DROPS], const rain_shelter_t *shelter, rain_tick_t tick);
void rain_visibility(pvr_particle_t drops[RAIN_DROPS], const rain_shelter_t *shelter);
#endif
