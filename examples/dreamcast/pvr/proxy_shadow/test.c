#undef NDEBUG
#include "proxy.h"
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void check(int x, int y, int z) {
    unsigned axes = (x != 0) + (y != 0) + (z != 0);
    shz_vec3_t light = shz_vec3_init(x, y, z);
    float length = sqrtf(shz_vec3_dot(light, light));
    light = shz_vec3_scale(light, 1.0f/length);
    proxy_volume_t out;
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, .875f, light, 3) == 0);
    assert(out.count == 4*axes + 2*(axes == 1 ? 4 : 6));
    unsigned edges[PROXY_POINTS][PROXY_POINTS] = {{0}};
    double signed_six_volume = 0;
    for(size_t i = 0; i < out.count; ++i) {
        const uint8_t *t = out.triangles[i];
        for(unsigned k = 0; k < 3; ++k) {
            unsigned a = t[k], b = t[(k+1)%3];
            assert(a < PROXY_POINTS && b < PROXY_POINTS && a != b);
            assert(++edges[a][b] == 1);
        }
        shz_vec3_t a = out.points[t[0]], b = out.points[t[1]], c = out.points[t[2]];
        shz_vec3_t n = shz_vec3_cross(shz_vec3_sub(b,a), shz_vec3_sub(c,a));
        assert(shz_vec3_dot(n,n) > 0);
        signed_six_volume += shz_vec3_dot(a, shz_vec3_cross(b,c));
    }
    for(unsigned i = 0; i < PROXY_POINTS; ++i)
        for(unsigned j = 0; j < PROXY_POINTS; ++j)
            assert(edges[i][j] == edges[j][i]);
    /* The directional sweep's volume is length times projected cap area. */
    double projected_area = (2*(x != 0) + 2*(y != 0) + 4*(z != 0)) / sqrt((double)axes);
    double expected = 6 * 3 * projected_area;
    assert(signed_six_volume > expected - 2e-4 && signed_six_volume < expected + 2e-4);
    unsigned char original[sizeof(out)];
    memcpy(original, &out, sizeof(out));
    assert(proxy_extrude(&out, out.count-1, .875f, light, 3) == -1 && errno == ENOSPC);
    assert(memcmp(original, &out, sizeof(out)) == 0);
    assert(proxy_extrude(&out, out.count, .875f, light, 3) == 0);
}

int main(void) {
    for(int x=-1; x<=1; ++x)
        for(int y=-1; y<=1; ++y)
            for(int z=-1; z<=1; ++z)
                if(x || y || z)
                    check(x, y, z);
    proxy_volume_t out;
    memset(&out, 0x5a, sizeof(out));
    unsigned char original[sizeof(out)];
    memcpy(original, &out, sizeof(out));
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, 1, shz_vec3_init(0,0,0), 3) == -1);
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, 1, shz_vec3_init(1,0,0), 0) == -1);
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, FLT_MAX, shz_vec3_init(0,0,1), 3) == -1);
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, 1, shz_vec3_init(0,0,1), FLT_MIN) == -1);
    assert(proxy_extrude(&out, PROXY_MAX_TRIANGLES, 1, shz_vec3_init(1,0,0), FLT_MAX) == -1);
    assert(memcmp(original, &out, sizeof(out)) == 0);
    puts("CLOSED PROXY TOPOLOGY PASS");
    return 0;
}
