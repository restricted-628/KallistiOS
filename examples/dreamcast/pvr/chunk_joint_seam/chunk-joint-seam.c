/* KallistiOS ##version##

   Position-only seams between separately skinned Compact models.
   Copyright (C) 2026 Joseph Black
*/

#ifndef CHUNK_JOINT_SEAM_HOST
#include <kos.h>
#include <dc/pvr_chunk_cache.h>
#endif

#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_skin_prepared.h>
#include <sh4zam/shz_trig.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CHUNK_JOINT_SEAM_HOST
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

#define CHECK(expr) do { \
    if(!(expr)) { \
        fprintf(stderr, "CHUNK JOINT SEAM FAIL line %d: %s\n", __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

enum { VERTICES = 4, JOINTS = 2, PAIRS = 2, POSES = 7, WIDTH = 256, HEIGHT = 128 };

#define MODEL_HEADER (PVR_CHUNK_VERTEX_XYZ_NORMAL | (25u << 16))
#define ARM_POINT(x, y) (x), (y), 0, 0, 0, 0x3f800000u
#define FOREARM_POINT(x, y) (x), (y), 0, 0, 0x3f800000u, 0
#define RIGID(index, joint) { (index), { (joint), 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 }

/* Independent local vertex spaces and independent strip corner attributes.
   The seam is x=0 in the reference pose. Forearm row order is deliberately
   different from the strip's order. */
static const uint32_t arm_vertices[] = {
    MODEL_HEADER, (VERTICES << 16) | 4,
    ARM_POINT(0xc2980000u, 0xc1400000u), ARM_POINT(0xc2980000u, 0x41400000u),
    ARM_POINT(0, 0xc1400000u), ARM_POINT(0, 0x41400000u),
    PVR_CHUNK_CONTROL_END
};
static const uint32_t forearm_vertices[] = {
    MODEL_HEADER, (VERTICES << 16) | 20,
    FOREARM_POINT(0, 0x41400000u), FOREARM_POINT(0x42980000u, 0xc1400000u),
    FOREARM_POINT(0, 0xc1400000u), FOREARM_POINT(0x42980000u, 0x41400000u),
    PVR_CHUNK_CONTROL_END
};
static const uint16_t arm_polygons[] = {
    PVR_CHUNK_STRIP_UV8_FIXED, 14, 1, VERTICES,
    4, 32, 32, 5, 32, 224, 6, 96, 32, 7, 96, 224, PVR_CHUNK_CONTROL_END
};
static const uint16_t forearm_polygons[] = {
    PVR_CHUNK_STRIP_UV8_FIXED, 14, 1, VERTICES,
    22, 160, 224, 20, 160, 32, 21, 224, 224, 23, 224, 32, PVR_CHUNK_CONTROL_END
};
static const pvr_chunk_model_t models[] = {
    { arm_vertices, sizeof(arm_vertices) / sizeof(*arm_vertices),
      arm_polygons, sizeof(arm_polygons) / sizeof(*arm_polygons), { 0, 0, 0 }, 78 },
    { forearm_vertices, sizeof(forearm_vertices) / sizeof(*forearm_vertices),
      forearm_polygons, sizeof(forearm_polygons) / sizeof(*forearm_polygons), { 0, 0, 0 }, 78 }
};
static const pvr_chunk_skin_influence_t influences[][VERTICES] = {
    { RIGID(4, 0), RIGID(5, 0), RIGID(6, 0), RIGID(7, 0) },
    { RIGID(20, 1), RIGID(21, 1), RIGID(22, 1), RIGID(23, 1) }
};
alignas(32) static const matrix_t identity = {
    { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
};

typedef struct mesh {
    pvr_chunk_vertex_index_entry_t indices[256];
    uint32_t lookup[256];
    alignas(32) uint8_t source_storage[384];
    pvr_chunk_skin_binding_t binding;
    pvr_chunk_skin_source_t source;
    pvr_skin_prepared_influence_t weights[VERTICES];
    pvr_skin_prepared_influences_t weight_plan;
    pvr_deform_vertex_t posed[VERTICES + 1];
    float uv[VERTICES][2];
#ifndef CHUNK_JOINT_SEAM_HOST
    alignas(32) uint8_t cache_storage[1024];
    pvr_chunk_cache_draw_t draw;
    pvr_poly_hdr_t header;
    size_t draw_vertices;
    uint32_t color;
#endif
} mesh_t;

typedef struct seam_pair {
    uint16_t anchor;
    uint16_t follower;
} seam_pair_t;

typedef struct seam_slots {
    unsigned anchor;
    unsigned follower;
} seam_slots_t;

static mesh_t arm, forearm;

static bool mesh_slot(const mesh_t *mesh, uint16_t index, unsigned *slot) {
    /* The source builder emits rows in the admitted influence table order. */
    for(unsigned row = 0; row < mesh->source.vertex_count; ++row) {
        if(mesh->binding.skin.influences[row].vertex_index == index) {
            *slot = row;
            return true;
        }
    }
    return false;
}

static bool prepare_seam(const mesh_t *anchor, const mesh_t *follower,
                         const seam_pair_t pairs[PAIRS], seam_slots_t out[PAIRS]) {
    seam_slots_t prepared[PAIRS];
    unsigned destinations = 0;

    for(unsigned i = 0; i < PAIRS; ++i) {
        if(!mesh_slot(anchor, pairs[i].anchor, &prepared[i].anchor) ||
           !mesh_slot(follower, pairs[i].follower, &prepared[i].follower))
            return false;

        const unsigned bit = 1u << prepared[i].follower;
        if(destinations & bit)
            return false;
        destinations |= bit;
    }
    memcpy(out, prepared, sizeof(prepared));
    return true;
}

/* Call only with two successful poses in the same coordinate space, in the
   same row order used to prepare the map. The two mesh buffers are distinct. */
static void apply_seam(const mesh_t *anchor, mesh_t *follower,
                       const seam_slots_t plan[PAIRS]) {
    for(unsigned i = 0; i < PAIRS; ++i)
        follower->posed[plan[i].follower].position = anchor->posed[plan[i].anchor].position;
}

static void prepare_mesh(mesh_t *mesh, unsigned part) {
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    const pvr_chunk_model_t *model = &models[part];
    const pvr_chunk_skin_t skin = { influences[part], VERTICES, JOINTS };

    CHECK(pvr_chunk_model_open(model, &view) == 0);
    CHECK(pvr_chunk_model_plan_build(&view, mesh->indices, 256, &plan) == 0);
    CHECK(pvr_chunk_skin_bind(&plan, &skin, mesh->lookup, 256, &mesh->binding) == 0);
    CHECK(pvr_chunk_skin_source_build(&mesh->binding, mesh->source_storage,
          sizeof(mesh->source_storage), &mesh->source) == 0);
    CHECK(mesh->source.vertex_count == VERTICES);

    const pvr_skin_stream_t weights = { mesh->source.influences, VERTICES,
                                        sizeof(*mesh->source.influences) };
    CHECK(pvr_skin_influences_prepare(&weights, JOINTS, mesh->weights, VERTICES,
                                       &mesh->weight_plan) == 0);

    pvr_chunk_iterator_t records;
    pvr_chunk_record_t record;
    pvr_chunk_strip_iterator_t strips;
    pvr_chunk_strip_view_t strip;
    CHECK(pvr_chunk_polygon_iterator_init(&records, model->polygon_words,
                                           model->polygon_word_count) == 0);
    CHECK(pvr_chunk_iterator_next(&records, &record) == 1);
    CHECK(pvr_chunk_strip_iterator_init(&strips, &record) == 0);
    CHECK(pvr_chunk_strip_iterator_next(&strips, &strip) == 1);
    CHECK(strip.vertex_count == VERTICES);
    for(unsigned i = 0; i < VERTICES; ++i) {
        pvr_chunk_strip_vertex_view_t corner;
        pvr_chunk_strip_attributes_t attr;
        unsigned slot;
        CHECK(pvr_chunk_strip_vertex_get(&strip, i, &corner) == 0);
        CHECK(pvr_chunk_strip_attributes_get(&strip, i, &attr) == 0);
        CHECK(mesh_slot(mesh, corner.index, &slot));
        memcpy(mesh->uv[slot], attr.uv[0], sizeof(mesh->uv[slot]));
    }

#ifndef CHUNK_JOINT_SEAM_HOST
    pvr_chunk_model_cache_t cache;
    CHECK(pvr_chunk_model_cache_build(&plan, mesh->cache_storage,
          sizeof(mesh->cache_storage), NULL, NULL, &cache) == 0);
    CHECK(pvr_chunk_model_cache_draw_prepare(&cache, &mesh->draw) == 0);
#endif
}

static void skin_mesh(mesh_t *mesh, const pvr_skin_prepared_palette_t *palette) {
    const pvr_deform_stream_t source = { mesh->source.vertices, VERTICES,
                                          sizeof(*mesh->source.vertices) };
    pvr_deform_result_t result;

    CHECK(pvr_skin_apply_prepared(mesh->posed, VERTICES, &source,
                                   &mesh->weight_plan, palette, &result) == 0);
    CHECK(result.deformed_vertices == VERTICES);
}

static shz_sincos_t sample_pose(unsigned frame, matrix_t bones[JOINTS],
                                pvr_normal_matrix_t normals[JOINTS]) {
    const float angle = ((float)frame - (float)(POSES / 2)) * 0.18f;
    const shz_sincos_t trig = shz_sincosf(angle);

    for(unsigned i = 0; i < JOINTS; ++i) {
        memcpy(bones[i], identity, sizeof(matrix_t));
        bones[i][3][0] = (float)(WIDTH / 2) + (float)frame;
        bones[i][3][1] = (float)(HEIGHT / 2);
    }
    bones[1][0][0] = bones[1][1][1] = trig.cos;
    bones[1][0][1] = trig.sin;
    bones[1][1][0] = -trig.sin;
    for(unsigned i = 0; i < JOINTS; ++i)
        CHECK(pvr_normal_matrix_build(&normals[i], &bones[i]) == 0);
    return trig;
}

#ifndef CHUNK_JOINT_SEAM_HOST
static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    mesh_t *mesh = data;
    (void)strip;
    return pvr_prim(&mesh->header, sizeof(mesh->header));
}

static int resolve_vertex(uint16_t index, pvr_deform_vertex_t *vertex, void *data) {
    const mesh_t *mesh = data;
    const pvr_chunk_skin_pose_t pose = { &mesh->binding, mesh->posed, VERTICES };
    return pvr_chunk_skin_pose_vertex_get(&pose, index, vertex);
}

static int verify_vertex(const pvr_chunk_render_state_t *state, uint16_t index,
                         const pvr_deform_vertex_t *deformed, pvr_vertex_t *vertex,
                         void *data) {
    mesh_t *mesh = data;
    unsigned slot;
    (void)state;
    CHECK(mesh_slot(mesh, index, &slot));
    CHECK(vertex->u == mesh->uv[slot][0] && vertex->v == mesh->uv[slot][1]);
    CHECK(memcmp(&deformed->normal, &mesh->posed[slot].normal,
                   sizeof(deformed->normal)) == 0);
    vertex->argb = mesh->color;
    ++mesh->draw_vertices;
    return 0;
}

static void draw_mesh(mesh_t *mesh, pvr_geometry_sink_t *sink) {
    alignas(32) pvr_vertex_t workspace[VERTICES];
    pvr_chunk_cache_result_t result;
    mesh->draw_vertices = 0;
    CHECK(pvr_chunk_model_cache_draw_emit(&mesh->draw, &identity, sink,
          workspace, VERTICES, NULL, begin_strip, resolve_vertex, verify_vertex,
          mesh, &result) == 0);
    CHECK(result.emitted_strips == 1 && result.emitted_vertices == VERTICES);
    CHECK(mesh->draw_vertices == VERTICES);
}

static void render_pose(const pvr_txr_surface_t *target, unsigned frame) {
    pvr_geometry_sink_t sink;
    pvr_render_ticket_t ticket;
    CHECK(pvr_geometry_sink_init_current(&sink) == 0);
    CHECK(pvr_wait_ready() == 0);
    CHECK(pvr_txr_surface_begin_render(target, WIDTH, HEIGHT) == 0);
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
    draw_mesh(&arm, &sink);
    draw_mesh(&forearm, &sink);
    CHECK(pvr_list_finish() == 0);
    CHECK(pvr_scene_finish_tracked(&ticket) == 0);
    CHECK(pvr_render_ticket_wait(&ticket, PVR_RENDER_STAGE_COMPLETE, 1000) == 0);

    const volatile uint16_t *pixels = target->vram;
    const unsigned seam_x = WIDTH / 2 + frame;
    for(unsigned y = HEIGHT / 2 - 6; y <= HEIGHT / 2 + 6; ++y) {
        CHECK((pixels[y * WIDTH + seam_x - 3] & 0x07e0u) >= 0x0200u);
        CHECK((pixels[y * WIDTH + seam_x + 3] & 0x001fu) >= 8u);
        for(unsigned x = seam_x - 2; x <= seam_x + 2; ++x) {
            const uint16_t pixel = pixels[y * WIDTH + x];
            CHECK(!(pixel & 0xf800u) && (pixel & 0x07ffu));
        }
    }
}
#endif

int main(void) {
    static const seam_pair_t authored[PAIRS] = { { 6, 22 }, { 7, 20 } };
    static const seam_pair_t invalid[PAIRS] = { { 6, 22 }, { 7, 21 + VERTICES } };
    static const seam_pair_t duplicate[PAIRS] = { { 6, 22 }, { 7, 22 } };
    seam_slots_t plan[PAIRS], saved[PAIRS];
    pvr_deform_vertex_t guard;
    uint8_t original_source[2][sizeof(arm.source_storage)];
    uint32_t original_vertices[2][sizeof(arm_vertices) / sizeof(*arm_vertices)];
    uint16_t original_polygons[2][sizeof(arm_polygons) / sizeof(*arm_polygons)];

    prepare_mesh(&arm, 0);
    prepare_mesh(&forearm, 1);
    CHECK(prepare_seam(&arm, &forearm, authored, plan));
    memcpy(saved, plan, sizeof(saved));
    CHECK(!prepare_seam(&arm, &forearm, invalid, plan));
    CHECK(!prepare_seam(&arm, &forearm, duplicate, plan));
    CHECK(memcmp(saved, plan, sizeof(saved)) == 0);
    for(unsigned i = 0; i < PAIRS; ++i) {
        CHECK(arm.uv[plan[i].anchor][0] == 0.375f);
        CHECK(forearm.uv[plan[i].follower][0] == 0.625f);
        CHECK(arm.uv[plan[i].anchor][1] != forearm.uv[plan[i].follower][1]);
    }

    memcpy(original_source[0], arm.source_storage, sizeof(arm.source_storage));
    memcpy(original_source[1], forearm.source_storage, sizeof(forearm.source_storage));
    for(unsigned i = 0; i < 2; ++i) {
        memcpy(original_vertices[i], models[i].vertex_words, sizeof(original_vertices[i]));
        memcpy(original_polygons[i], models[i].polygon_words, sizeof(original_polygons[i]));
    }
    memset(&guard, 0x5a, sizeof(guard));
    memcpy(&arm.posed[VERTICES], &guard, sizeof(guard));
    memcpy(&forearm.posed[VERTICES], &guard, sizeof(guard));

#ifndef CHUNK_JOINT_SEAM_HOST
    pvr_poly_cxt_t context;
    pvr_txr_surface_t target;
    pvr_pipeline_status_t pipeline;
    alignas(32) uint16_t gray[8 * 8];
    CHECK(pvr_init_defaults() == 0);
    pvr_set_bg_color(1.0f, 0.0f, 0.0f);
    CHECK(pvr_txr_surface_alloc(&target, WIDTH, HEIGHT, PVR_TXR_SURFACE_RGB565,
                                 PVR_TXR_SURFACE_LINEAR, false) == 0);
    pvr_ptr_t texture = pvr_mem_malloc(sizeof(gray));
    CHECK(texture != NULL);
    for(unsigned i = 0; i < 8 * 8; ++i)
        gray[i] = (i & 8u) ? 0xbdf7u : 0xffffu;
    CHECK(pvr_txr_load_ex_checked(gray, texture, 8, 8, PVR_TXRLOAD_16BPP) == 0);
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                     8, 8, texture, PVR_FILTER_NEAREST);
    context.gen.culling = PVR_CULLING_NONE;
    context.txr.env = PVR_TXRENV_MODULATE;
    pvr_poly_compile(&arm.header, &context);
    pvr_poly_compile(&forearm.header, &context);
    arm.color = 0xff00ff00u;
    forearm.color = 0xff0000ffu;
#endif

    bool gap_without_seam = false;
    for(unsigned frame = 0; frame < POSES; ++frame) {
        alignas(32) matrix_t bones[JOINTS];
        pvr_normal_matrix_t normals[JOINTS];
        pvr_skin_prepared_joint_t joints[JOINTS];
        pvr_skin_prepared_palette_t prepared;
        const pvr_skin_palette_t palette = { bones, normals, JOINTS };
        const shz_sincos_t trig = sample_pose(frame, bones, normals);
        CHECK(pvr_skin_palette_prepare(&palette, joints, JOINTS, &prepared) == 0);
        skin_mesh(&arm, &prepared);
        skin_mesh(&forearm, &prepared);

        pvr_deform_vertex_t original_pose[VERTICES];
        memcpy(original_pose, forearm.posed, sizeof(original_pose));
        for(unsigned i = 0; i < PAIRS; ++i) {
            const point_t a = arm.posed[plan[i].anchor].position;
            const point_t b = original_pose[plan[i].follower].position;
            gap_without_seam |= fabsf(a.x - b.x) > 3.0f;
        }

        apply_seam(&arm, &forearm, plan);
        for(unsigned i = 0; i < PAIRS; ++i) {
            const pvr_deform_vertex_t *a = &arm.posed[plan[i].anchor];
            const pvr_deform_vertex_t *b = &forearm.posed[plan[i].follower];
            CHECK(memcmp(&a->position, &b->position, sizeof(a->position)) == 0);
            CHECK(a->position.x == (float)(WIDTH / 2 + frame) && a->position.w == 1.0f);
            CHECK(a->normal.z > 0.999f && fabsf(b->normal.z) < 0.001f);
            CHECK(fabsf(b->normal.x + trig.sin) < 0.001f);
            CHECK(fabsf(b->normal.y - trig.cos) < 0.001f && b->normal.w == 0.0f);
        }
        for(unsigned row = 0; row < VERTICES; ++row) {
            CHECK(memcmp(&original_pose[row].normal, &forearm.posed[row].normal,
                         sizeof(original_pose[row].normal)) == 0);
            if(row != plan[0].follower && row != plan[1].follower)
                CHECK(memcmp(&original_pose[row], &forearm.posed[row],
                             sizeof(original_pose[row])) == 0);
        }
        CHECK(memcmp(&arm.posed[VERTICES], &guard, sizeof(guard)) == 0);
        CHECK(memcmp(&forearm.posed[VERTICES], &guard, sizeof(guard)) == 0);
#ifndef CHUNK_JOINT_SEAM_HOST
        render_pose(&target, frame);
#endif
    }
    CHECK(gap_without_seam);
    CHECK(memcmp(original_source[0], arm.source_storage, sizeof(arm.source_storage)) == 0);
    CHECK(memcmp(original_source[1], forearm.source_storage, sizeof(forearm.source_storage)) == 0);
    for(unsigned i = 0; i < 2; ++i) {
        CHECK(memcmp(original_vertices[i], models[i].vertex_words, sizeof(original_vertices[i])) == 0);
        CHECK(memcmp(original_polygons[i], models[i].polygon_words, sizeof(original_polygons[i])) == 0);
    }

#ifndef CHUNK_JOINT_SEAM_HOST
    CHECK(pvr_get_pipeline_status(&pipeline) == 0);
    CHECK(pipeline.faults.mask == PVR_FAULT_NONE);
    pvr_mem_free(texture);
    pvr_txr_surface_release(&target);
    CHECK(pvr_shutdown() == 0);
    printf("CHUNK JOINT SEAM RTT PASS poses=%u\n", POSES);
    for(;;)
        thd_sleep(1000);
#else
    printf("CHUNK JOINT SEAM HOST PASS poses=%u\n", POSES);
#endif
    return 0;
}
