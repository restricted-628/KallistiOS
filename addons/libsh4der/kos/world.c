/* KallistiOS ##version## */
#include <sh4der_kos_world.h>
#include <sh4der/sh4der_guard.h>
#include <sh4dow_heightfield.h>
#include <errno.h>
#include <limits.h>

int sh4der_kos_pool_vertices(const sh4der_pool_vtx_t *fan, unsigned count, bool closed,
                             pvr_vertex_t *out, size_t capacity) {
    if(!fan || !out || count < (closed ? 4u : 3u) ||
       count > SH4DER_POOL_MAX_N + (closed ? 1u : 2u)) {
        errno = EINVAL;
        return -1;
    }
    unsigned triangles = count - (closed ? 1u : 2u);
    if(capacity < 3 * triangles) {
        errno = ENOSPC;
        return -1;
    }
    for(unsigned i = 0; i < triangles; ++i) {
        unsigned idx[3] = {0, i + 1, i + 2 == count ? 1 : i + 2};
        for(unsigned j = 0; j < 3; ++j) {
            const sh4der_pool_vtx_t *p = &fan[idx[j]];
            *out++ = (pvr_vertex_t){.x = p->x,
                                    .y = p->y,
                                    .z = p->z,
                                    .argb = p->argb,
                                    .flags = j == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX};
        }
    }
    return (int)(3 * triangles);
}

int sh4der_kos_spark_vertices(const sh4der_spark_t *s, const sh4der_spark_view_t *v,
                              const sh4der_spark_screen_t *screen, pvr_vertex_t out[4]) {
    if(!s || !v || !screen || !out) {
        errno = EINVAL;
        return -1;
    }
    sh4der_spark_vtx_t q[4];
    if(!sh4der_spark_quad(s, v, q))
        return 0;
    float xy[4][2], iz[4];
    for(unsigned i = 0; i < 4; ++i) {
        if(!sh4der_finite_(q[i].z))
            return 0;
        iz[i] = shz_invf(q[i].z);
        xy[i][0] = q[i].x * iz[i];
        xy[i][1] = q[i].y * iz[i];
    }
    for(unsigned i = 0; i < 2; ++i) {
        const float *a = xy[i], *b = xy[i + 1], *c = xy[i + 2];
        float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if(!(shz_fabsf(area) >= SH4DER_GUARD_MIN_AREA2) || !sh4der_finite_(area))
            return 0;
    }
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (pvr_vertex_t){.flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                                .x = shz_fmaf(xy[i][0], v->focal_px, screen->cx),
                                .y = shz_fmaf(xy[i][1], -screen->focal_y_px, screen->cy),
                                .z = iz[i],
                                .u = q[i].u,
                                .v = q[i].v,
                                .argb = q[i].argb};
    return 4;
}

void sh4der_kos_billboard_y(matrix_t *out, shz_vec3_t pos, shz_vec3_t eye) {
    shz_mat4x4_t saved, model;
    shz_xmtrx_store_4x4(&saved);
    shz_mat4x4_init_billboard_y(&model, pos, eye);
    /* Upstream builds the facing rotation only; this is a model transform. */
    model.col[3] = shz_vec4_init(pos.x, pos.y, pos.z, 1);
    shz_kos_matrix_export(out, &model);
    shz_xmtrx_load_4x4(&saved);
}

int sh4der_kos_mirror_view(sh4der_pl4nar_view_t *out, matrix_t *projection,
                           const sh4der_pl4nar_plane_t *plane, const float eye[3],
                           const float target[3], const float up[3], float fov, float near_z,
                           unsigned w, unsigned h, float fade) {
    if(!out || !projection || !plane || !eye || !target || !up || !(near_z > 0)) {
        errno = EINVAL;
        return -1;
    }
    shz_mat4x4_t saved, matrix;
    shz_xmtrx_store_4x4(&saved);
    int ok = sh4der_pl4nar_view_build(out, plane, eye, target, up, fov, near_z, w, h, fade);
    if(ok) {
        shz_xmtrx_store_4x4(&matrix);
        shz_kos_matrix_export(projection, &matrix);
    }
    shz_xmtrx_load_4x4(&saved);
    if(!ok) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sh4der_kos_mirror_header(pvr_poly_hdr_t *out, const sh4der_kos_trail_history_t *h,
                             sh4der_kos_frame_id_t id, pvr_ptr_t dst, bool additive) {
    if(!out || !h || !h->image || !h->address || dst == h->address) {
        errno = EINVAL;
        return -1;
    }
    if(!id.render || id.render != h->id.render || id.generation != h->id.generation) {
        errno = ESTALE;
        return -1;
    }
    const pvr_txr_surface_t *s = h->image->surface;
    unsigned tw = 8;
    while(tw < s->width)
        tw *= 2;
    pvr_poly_cxt_t c;
    pvr_poly_cxt_txr(&c, PVR_LIST_TR_POLY, pvr_txr_surface_pvr_format(s), tw, s->height, h->address,
                     PVR_FILTER_BILINEAR);
    c.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    c.depth.write = PVR_DEPTHWRITE_DISABLE;
    c.gen.fog_type = PVR_FOG_DISABLE;
    c.gen.culling = PVR_CULLING_NONE;
    c.txr.env = PVR_TXRENV_MODULATEALPHA;
    c.txr.uv_clamp = PVR_UVCLAMP_UV;
    c.blend.src = PVR_BLEND_SRCALPHA;
    c.blend.dst = additive ? PVR_BLEND_ONE : PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(out, &c);
    return 0;
}

int sh4der_kos_mirror_samples(const sh4der_pl4nar_view_t *v, const sh4der_pl4nar_material_t *m,
                              const float (*pos)[3], const float (*n)[3],
                              sh4der_pl4nar_sample_t *out, unsigned count) {
    if(!v || !m || !pos || !out) {
        errno = EINVAL;
        return -1;
    }
    shz_mat4x4_t saved;
    shz_xmtrx_store_4x4(&saved);
    if(n)
        sh4der_pl4nar_shade_n(v, *pos, sizeof(*pos), *n, sizeof(*n), m, out, count);
    else
        sh4der_pl4nar_flat_n(v, *pos, sizeof(*pos), m, out, NULL, count);
    shz_xmtrx_load_4x4(&saved);
    return 0;
}

int sh4der_kos_mirror_vertex(const pvr_chunk_render_state_t *state,
                             const pvr_chunk_vertex_attributes_t *a,
                             const pvr_chunk_strip_attributes_t *strip, pvr_vertex_t *v,
                             void *data) {
    (void)state;
    (void)strip;
    const sh4der_kos_mirror_draw_t *d = data;
    const sh4der_pl4nar_sample_t *s = d->samples + a->index;
    const float *uv = d->image->uv;
    v->u = shz_fmaf(s->u, uv[2], uv[0]);
    v->v = shz_fmaf(s->v, uv[3], uv[1]);
    v->argb = sh4der_pl4nar_argb(s, d->material);
    return 0;
}

void sh4der_kos_water_sample(const sh4der_kos_water_material_t *m, float x, float z, float time,
                             float footprint, const float view[3], const float light[3],
                             float crest, float visibility, sh4der_kos_surface_sample_t *out,
                             float normal[3]) {
    sh4der_water_surface_t s =
        sh4der_water_sample(m->waves, m->count, x, z, time, footprint, m->swell, m->ripple);
    sh4der_water_normal(s, normal);
    out->dy = s.height;
    float entry = 1.0f - sh4der_water_fresnel(sh4der_dot3_(view, normal));
    sh4der_water_crest_sss(&m->scattering, view, light, normal, crest, m->thin_path, m->thick_path,
                           entry, visibility, out->transmission);
}

int sh4der_kos_surface_vertex(const pvr_chunk_render_state_t *state,
                              const pvr_chunk_vertex_attributes_t *a,
                              const pvr_chunk_strip_attributes_t *strip, pvr_vertex_t *v,
                              void *data) {
    (void)state;
    (void)strip;
    const sh4der_kos_surface_sample_t *s = (const sh4der_kos_surface_sample_t *)data + a->index;
    v->y += s->dy;
    uint32_t rgb = v->argb & 0xff000000;
    for(unsigned i = 0; i < 3; ++i) {
        unsigned shift = 16 - 8 * i;
        float c = (float)((v->argb >> shift) & 255) + 255.0f * s->transmission[i];
        rgb |= (uint32_t)(shz_clampf(c, 0, 255) + 0.5f) << shift;
    }
    v->argb = rgb;
    return 0;
}

int sh4der_kos_heightfield_ao(const float *heights, unsigned w, unsigned h, float cell,
                              const float sun[3], float bias, float feather, float *tmp,
                              float *scratch, uint8_t *out) {
    /* Upstream interpolates grid coordinates as float before indexing. */
    if(!out || w < 2 || h < 2 || w > (1u << 24) || h > (1u << 24) ||
       w > SIZE_MAX / sizeof(float) / h || w > UINT_MAX / h ||
       !sh4dow_heightfield_sun(heights, w, h, cell, sun, bias, feather, tmp, scratch)) {
        errno = EINVAL;
        return -1;
    }
    for(unsigned i = 0; i < w * h; ++i)
        out[i] = (uint8_t)((1.0f - tmp[i]) * 255.0f + 0.5f);
    return 0;
}
