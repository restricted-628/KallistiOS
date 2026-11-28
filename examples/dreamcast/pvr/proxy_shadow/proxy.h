#ifndef EXAMPLE_PROXY_SHADOW_H
#define EXAMPLE_PROXY_SHADOW_H
#include <sh4zam/shz_vector.h>
#include <stddef.h>
#include <stdint.h>

/* A cube has at most three light-facing quads and six silhouette edges. */
enum { PROXY_MAX_TRIANGLES = 24, PROXY_POINTS = 16 };
typedef struct proxy_volume {
    shz_vec3_t points[PROXY_POINTS];
    uint8_t triangles[PROXY_MAX_TRIANGLES][3];
    size_t count;
} proxy_volume_t;

/* Unit vector points toward the light. Failure leaves out unchanged. */
int proxy_extrude(proxy_volume_t *out, size_t capacity, float front_z,
                  shz_vec3_t light, float length);
#endif
