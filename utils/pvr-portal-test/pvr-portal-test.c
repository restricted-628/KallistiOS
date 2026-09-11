/* KallistiOS ##version##
   Software coverage goldens for the shared portal fixture.
   Copyright (C) 2026 Joseph Black
*/
#include "portal-scene.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t pixels[480][640];
static float depths[480][640];

static double edge(const pvr_vertex_t *a, const pvr_vertex_t *b,
                    double x, double y) {
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static void raster(const pvr_vertex_t *a) {
    const pvr_vertex_t *b = a + 1, *c = a + 2;
    const double area = edge(a, b, c->x, c->y);
    if(area == 0)
        return;
    assert(a->argb == b->argb && b->argb == c->argb);
    for(unsigned y = 0; y < 480; ++y) {
        for(unsigned x = 0; x < 640; ++x) {
            const double u = edge(b, c, x + .5, y + .5) / area;
            const double v = edge(c, a, x + .5, y + .5) / area;
            const double w = edge(a, b, x + .5, y + .5) / area;
            if(u < 0 || v < 0 || w < 0)
                continue;
            const float z = (float)(u * a->z + v * b->z + w * c->z);
            if(z > depths[y][x]) {
                depths[y][x] = z;
                pixels[y][x] = a->argb;
            }
        }
    }
}

static void render(const portal_scene_t *scene, bool clear_middle) {
    memset(depths, 0, sizeof(depths));
    for(unsigned y = 0; y < 480; ++y)
        for(unsigned x = 0; x < 640; ++x)
            pixels[y][x] = 0xff0000ff;
    for(size_t pass = 0; pass < 3; ++pass) {
        if(pass == 1 && clear_middle)
            memset(depths, 0, sizeof(depths));
        assert(scene->count[pass] <= PORTAL_PASS_CAPACITY);
        assert(scene->count[pass] % 3 == 0);
        for(size_t i = 0; i < scene->count[pass]; i += 3) {
            const pvr_vertex_t *triangle = scene->vertices[pass] + i;
            assert(triangle[0].flags == PVR_CMD_VERTEX);
            assert(triangle[1].flags == PVR_CMD_VERTEX);
            assert(triangle[2].flags == PVR_CMD_VERTEX_EOL);
            raster(triangle);
        }
    }
}

/* Analytic screen rectangles, independent of the triangle compiler and its
   corner ordering. Full-frame comparison catches holes, overdraw outside the
   opening, wrong W depth, missing foreground replay and a late extra clear. */
static uint32_t expected(unsigned x, unsigned y) {
    if(x < 40 || x >= 600 || y < 40 || y >= 440)
        return 0xff0000ff;
    if(x >= 304 && x < 336 && y >= 80 && y < 400)
        return 0xff00ffff;
    if(x >= 193 && x < 447 && y >= 129 && y < 351) {
        if(x >= 360 && y >= 200 && y < 280)
            return 0xffff00ff;
        return 0xff00ff00;
    }
    return 0xffff0000;
}

static void check_image(void) {
    for(unsigned y = 0; y < 480; ++y) {
        for(unsigned x = 0; x < 640; ++x) {
            if(pixels[y][x] != expected(x, y)) {
                fprintf(stderr, "pixel %u,%u: %08x != %08x\n", x, y,
                        pixels[y][x], expected(x, y));
                assert(0);
            }
        }
    }
}

static void check_remote_attributes(const portal_scene_t *scene) {
    for(size_t i = 0; i < scene->count[1]; ++i) {
        const pvr_vertex_t *v = scene->vertices[1] + i;
        if(v->argb == 0xff00ffff)
            continue; /* Replayed main-view foreground, already projected. */
        assert(v->x >= 193 && v->x <= 447);
        assert(v->y >= 129 && v->y <= 351);
        double u, texture_v;
        if(v->argb == 0xff00ff00) {
            assert(v->z == .25f);
            u = v->x / 640.0;
            texture_v = v->y / 480.0;
        }
        else {
            assert(v->argb == 0xffff00ff && v->z == .5f);
            u = (v->x - 360.0) / 200.0;
            texture_v = (v->y - 200.0) / 80.0;
        }
        assert(fabs(v->u - u) < 1e-6 && fabs(v->v - texture_v) < 1e-6);
    }
}

int main(void) {
    portal_scene_t scene;
    errno = 0;
    assert(portal_scene_build(NULL, false, true) == -1 && errno == EINVAL);
    for(unsigned clear = 0; clear < 2; ++clear) {
        assert(portal_scene_build(&scene, clear != 0, true) == 0);
        check_remote_attributes(&scene);
        render(&scene, clear != 0);
        check_image();
    }
    /* Prove the oracle detects the two load-bearing composition mistakes. */
    assert(portal_scene_build(&scene, true, false) == 0);
    render(&scene, true);
    assert(pixels[240][320] == 0xff00ff00);
    assert(pixels[240][320] != expected(320, 240));
    assert(portal_scene_build(&scene, true, true) == 0);
    render(&scene, false);
    assert(pixels[240][240] == 0xffff0000);
    assert(pixels[240][400] == 0xffff0000);
    assert(pixels[240][240] != expected(240, 240));
    puts("portal coverage, attributes, and negative controls passed");
    return 0;
}
