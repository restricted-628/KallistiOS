/* Independent double-precision oracle for the Compact cone-light callback. */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sh4der_kos_spotlight.h>

static double reference(const sh4der_kos_spotlight_t *l, const pvr_deform_vertex_t *v) {
    double x = (double)v->position.x - l->position.x;
    double y = (double)v->position.y - l->position.y;
    double z = (double)v->position.z - l->position.z;
    double distance = sqrt(x*x + y*y + z*z);
    if(distance == 0 || distance > l->radius)
        return 0;
    double cosine = (x*l->axis.x + y*l->axis.y + z*l->axis.z) / distance;
    double diffuse = -(x*v->normal.x + y*v->normal.y + z*v->normal.z) / distance;
    double cone = fmax(0, fmin(1, (cosine - l->cutoff_cos) / (1.0 - l->cutoff_cos)));
    double atten = l->constant + l->linear * distance + l->quadratic * distance * distance;
    return fmin(1, cone*fmax(0, fmin(1, diffuse))/atten);
}

static void compare(const sh4der_kos_spotlight_plan_t *plan, pvr_deform_vertex_t v) {
    pvr_vertex_t in = {.flags=PVR_CMD_VERTEX_EOL,.x=3,.y=2,.z=1,.u=.1f,.v=.8f,
                       .argb=0x7bcfdbaf,.oargb=0xef635721};
    pvr_vertex_t out = in;
    pvr_chunk_cache_prepare_vertex_t callback = sh4der_kos_spotlight_vertex;
    assert(callback(NULL, 7, &v, &out, (void *)plan) == 0);
    double strength = reference(&plan->light, &v);
    for(unsigned shift = 0; shift <= 16; shift += 8) {
        double tint = (double)((plan->light.rgb >> shift) & 255u) / 255;
        int want = (int)floor(((in.argb >> shift) & 255u)*tint*strength + .5);
        int got = (int)((out.argb >> shift) & 255u);
        assert(abs(want-got) <= 1);
        if(want == 0 && strength == 0)
            assert(got == 0);
    }
    assert((in.argb >> 24) == (out.argb >> 24));
    out.argb = in.argb;
    assert(memcmp(&in, &out, sizeof(in)) == 0);
}

static void reject(sh4der_kos_spotlight_plan_t *plan, sh4der_kos_spotlight_t bad) {
    unsigned char before[sizeof(*plan)];
    memcpy(before, plan, sizeof(before));
    assert(sh4der_kos_spotlight_prepare(plan, &bad) == -1);
    assert(memcmp(plan, before, sizeof(before)) == 0);
}

int main(void) {
    sh4der_kos_spotlight_t light = {.axis={.z=1},.cutoff_cos=.5f,.radius=6,
        .constant=1,.linear=.25f,.quadratic=.125f,.rgb=0xff9753};
    sh4der_kos_spotlight_plan_t plan;
    assert(sh4der_kos_spotlight_prepare(&plan, &light) == 0);
    for(int x=-8; x<=8; ++x)
        for(int y=-4; y<=4; ++y)
            for(int z=-4; z<=8; ++z)
                compare(&plan, (pvr_deform_vertex_t){.position={.x=x,.y=y,.z=z},.normal={.z=-1}});
    compare(&plan, (pvr_deform_vertex_t){.position={.z=2},.normal={.z=1}});
    light.position = shz_vec3_init(4,5,6);
    light.axis = shz_vec3_init(1,0,0);
    light.cutoff_cos = 0;
    assert(sh4der_kos_spotlight_prepare(&plan, &light) == 0);
    compare(&plan, (pvr_deform_vertex_t){.position={.x=4,.y=6,.z=6},.normal={.y=-1}});
    compare(&plan, (pvr_deform_vertex_t){.position={.x=10,.y=5,.z=6},.normal={.x=-1}});
    compare(&plan, (pvr_deform_vertex_t){.position={.x=10.01f,.y=5,.z=6},.normal={.x=-1}});
    sh4der_kos_spotlight_t bad = light;
    bad.axis.x = 0; reject(&plan, bad);
    bad = light; bad.position.y = NAN; reject(&plan, bad);
    bad = light; bad.cutoff_cos = 1; reject(&plan, bad);
    bad = light; bad.radius = FLT_MAX; reject(&plan, bad);
    bad = light; bad.constant = 0; reject(&plan, bad);
    bad = light; bad.linear = -1; reject(&plan, bad);
    bad = light; bad.quadratic = INFINITY; reject(&plan, bad);
    bad = light; bad.rgb = 0x1000000; reject(&plan, bad);
    assert(sh4der_kos_spotlight_prepare(NULL, &light) == -1 && errno == EINVAL);
    assert(sh4der_kos_spotlight_prepare(&plan, NULL) == -1 && errno == EINVAL);
    puts("COMPACT SPOTLIGHT ORACLE PASS");
    return 0;
}
