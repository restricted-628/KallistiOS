#include "proxy.h"
#include <errno.h>
#include <math.h>
#include <stdbool.h>

struct face {
    uint8_t corners[4], neighbors[4];
    shz_vec3_t normal;
};
#include "proxy-data.inc"

static void triangle(proxy_volume_t *out, unsigned a, unsigned b, unsigned c) {
    uint8_t *t = out->triangles[out->count++];
    t[0] = a;
    t[1] = b;
    t[2] = c;
}

int proxy_extrude(proxy_volume_t *out, size_t capacity, float front_z,
                  shz_vec3_t light, float length) {
    if(!out || !isfinite(front_z) || !(length > 0 && isfinite(length)) ||
       !(fabsf(shz_vec3_dot(light, light) - 1.0f) <= .001f)) {
        errno = EINVAL;
        return -1;
    }
    if(front_z + 1.0f == front_z) {
        errno = ERANGE;
        return -1;
    }
    bool lit[6];
    for(unsigned f = 0; f < 6; ++f)
        lit[f] = shz_vec3_dot(faces[f].normal, light) > 0;
    proxy_volume_t volume = {0};
    shz_vec3_t away = shz_vec3_scale(light, -length);
    for(unsigned i = 0; i < 8; ++i) {
        volume.points[i] = corners[i];
        volume.points[i].z += front_z;
        volume.points[i+8] = shz_vec3_add(volume.points[i], away);
        shz_vec3_t near = volume.points[i], far = volume.points[i+8];
        if(!isfinite(far.x) || !isfinite(far.y) || !isfinite(far.z) ||
           (light.x != 0 && near.x == far.x) ||
           (light.y != 0 && near.y == far.y) ||
           (light.z != 0 && near.z == far.z)) {
            errno = ERANGE;
            return -1;
        }
    }
    /* The translated cube must still span all three axes in float. */
    if(volume.points[8].x >= volume.points[9].x ||
       volume.points[8].y >= volume.points[11].y ||
       volume.points[8].z >= volume.points[12].z) {
        errno = ERANGE;
        return -1;
    }
    for(unsigned f = 0; f < 6; ++f) {
        if(!lit[f])
            continue;
        const uint8_t *v = faces[f].corners;
        triangle(&volume, v[0], v[1], v[2]);
        triangle(&volume, v[0], v[2], v[3]);
        triangle(&volume, v[2]+8, v[1]+8, v[0]+8);
        triangle(&volume, v[3]+8, v[2]+8, v[0]+8);
        for(unsigned edge = 0; edge < 4; ++edge) {
            if(lit[faces[f].neighbors[edge]])
                continue;
            unsigned a = v[edge], b = v[(edge+1)%4];
            triangle(&volume, b, a, a+8);
            triangle(&volume, b, a+8, b+8);
        }
    }
    if(volume.count > capacity) {
        errno = ENOSPC;
        return -1;
    }
    *out = volume;
    return 0;
}
