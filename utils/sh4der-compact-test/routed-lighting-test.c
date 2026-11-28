#ifdef NDEBUG
#undef NDEBUG
#endif
#include <sh4der_kos_routed_lighting.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const pvr_chunk_cache_prepare_two_volume_vertex_t prepare = sh4der_kos_routed_lighting_vertex;
    const pvr_geometry_vertex_format_t textured = PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED;
    const pvr_geometry_vertex_format_t color = PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR;
    sh4der_kos_routed_directional_light_t lights[] = {
        {shz_vec3_init(0, 0, 1), {1, 0, 0},  0.75f, false},
        {shz_vec3_init(0, 0, 1), {1, 0, 0}, -0.25f, false},
        {shz_vec3_init(0, 0, -1), {0, 0, 1}, 0.5f, true}
    };
    const sh4der_kos_routed_masks_t masks = {{3, 4}, {4, 1}};
    sh4der_kos_routed_lighting_plan_t plan;
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 3, masks, textured, NULL) == 0);

    const pvr_deform_vertex_t deformation = {
        .position={2, 3, 4, 1},
        .normal={0, 0, 1, 0}
    };
    pvr_chunk_two_volume_vertex_t vertex = {.textured={
        .flags=PVR_CMD_VERTEX_EOL, .x=2, .y=3, .z=4,
        .argb0=0x7f112233u, .argb1=0x9f445566u,
        .oargb0=0x56010203u, .oargb1=0x78040506u,
        .u0=0.25f, .v0=0.75f, .u1=0.75f, .v1=0.25f,
        .d1=17, .d2=23, .d3=31, .d4=47
    }};
    const pvr_chunk_two_volume_vertex_t expected = {.textured={
        .flags=PVR_CMD_VERTEX_EOL, .x=2, .y=3, .z=4,
        .argb0=0x7f800000u, .argb1=0x9f000080u,
        .oargb0=0x56000080u, .oargb1=0x78bf0000u,
        .u0=0.25f, .v0=0.75f, .u1=0.75f, .v1=0.25f,
        .d1=17, .d2=23, .d3=31, .d4=47
    }};
    assert(prepare(NULL, 0, &deformation,
        PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, &vertex, &plan) == 0);
    assert(memcmp(&vertex, &expected, sizeof(vertex)) == 0);

    const sh4der_kos_routed_masks_t base_only = {{3, 0}, {4, 0}};
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 3, masks, color, NULL) == -1 && errno == EINVAL);
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 3, base_only, color, NULL) == 0);
    memset(&vertex, 0xa5, sizeof(vertex));
    vertex.color.argb0 = 0x7f112233;
    vertex.color.argb1 = 0x9f445566;
    pvr_chunk_two_volume_vertex_t expected_color;
    memcpy(&expected_color, &vertex, sizeof(vertex));
    expected_color.color.argb0 = 0x7f800000;
    expected_color.color.argb1 = 0x9f000080;
    assert(prepare(NULL, 0, &deformation, color, &vertex, &plan) == 0);
    assert(memcmp(&vertex, &expected_color, sizeof(vertex)) == 0);

    unsigned char before[sizeof(plan)];
    memcpy(before, &plan, sizeof(before));
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 2, masks, textured, NULL) == -1 && errno == EINVAL);
    assert(memcmp(before, &plan, sizeof(before)) == 0);
    lights[1].direction.x = NAN;
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 3, masks, textured, NULL) == -1 && errno == EDOM);
    assert(memcmp(before, &plan, sizeof(before)) == 0);

    const sh4der_kos_routed_masks_t blue_only = {{4, 0}, {0, 4}};
    assert(sh4der_kos_routed_lighting_prepare(&plan, lights, 3, blue_only, textured, NULL) == 0);
    assert(plan.count == 1 && plan.thin == 1);
    vertex = (pvr_chunk_two_volume_vertex_t){0};
    assert(prepare(NULL, 0, &deformation,
        PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, &vertex, &plan) == 0);
    assert(vertex.textured.argb0 == 0x80 && vertex.textured.oargb1 == 0x80);
    assert(vertex.textured.argb1 == 0 && vertex.textured.oargb0 == 0);

    const sh4der_kos_routed_directional_light_t axes[] = {
        {shz_vec3_init(1, 0, 0), {1, 0, 0}, 1, false},
        {shz_vec3_init(0, 1, 0), {0, 1, 0}, 1, false},
        {shz_vec3_init(0, 0, -1), {0, 0, 1}, 1, true}
    };
    const pvr_deform_vertex_t angled = {.normal={2.0f/7, 3.0f/7, 6.0f/7, 0}};
    const sh4der_kos_routed_masks_t full = {{7, 0}, {0, 0}};
    const sh4der_kos_routed_masks_t sparse = {{5, 0}, {0, 0}};
    assert(sh4der_kos_routed_lighting_prepare(&plan, axes, 3, full, textured, NULL) == 0);
    assert(sh4der_kos_routed_lighting_vertex(NULL, 0, &angled, textured, &vertex, &plan) == 0);
    assert(vertex.textured.argb0 == 0x496ddbu);
    assert(sh4der_kos_routed_lighting_prepare(&plan, axes, 3, sparse, textured, NULL) == 0);
    assert(sh4der_kos_routed_lighting_vertex(NULL, 0, &angled, textured, &vertex, &plan) == 0);
    assert(vertex.textured.argb0 == 0x4900dbu);

    const sh4der_kos_routed_masks_t no_lights = {{0}};
    sh4der_kos_routed_ambient_t ambient = {.outside={0x123456, 0x010203}};
    assert(sh4der_kos_routed_lighting_prepare(&plan, NULL, 0, no_lights, textured, &ambient) == 0);
    assert(sh4der_kos_routed_lighting_vertex(NULL, 0, &angled, textured, &vertex, &plan) == 0);
    assert(vertex.textured.argb0 == 0x123456 && vertex.textured.argb1 == 0x123456);
    assert(vertex.textured.oargb0 == 0x010203 && vertex.textured.oargb1 == 0x010203);
    ambient.separate_inside = true;
    assert(sh4der_kos_routed_lighting_prepare(&plan, NULL, 0, no_lights, textured, &ambient) == 0);
    assert(sh4der_kos_routed_lighting_vertex(NULL, 0, &angled, textured, &vertex, &plan) == 0);
    assert(vertex.textured.argb0 == 0x123456 && vertex.textured.argb1 == 0);
    assert(vertex.textured.oargb1 == 0);
    assert(sh4der_kos_routed_lighting_prepare(&plan, NULL, 0, no_lights, color, &ambient) == -1);
    assert(errno == EINVAL);

    memcpy(before, &plan, sizeof(before));
    ambient.inside[0] = 0x01000000;
    assert(sh4der_kos_routed_lighting_prepare(&plan, NULL, 0, no_lights,
                                              textured, &ambient) == -1 && errno == EINVAL);
    assert(memcmp(before, &plan, sizeof(before)) == 0);
    assert(sh4der_kos_routed_lighting_prepare(&plan, NULL, 0, no_lights,
                PVR_GEOMETRY_VERTEX_CANONICAL, NULL) == -1 && errno == EINVAL);
    assert(memcmp(before, &plan, sizeof(before)) == 0);

    puts("COMPACT ROUTED LIGHTING PACKETS PASS");
    return 0;
}
