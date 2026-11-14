/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_glow.h>
#include <errno.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(sh4der_kos_glow_strip_t) ==
    32 * (1 + SH4DER_KOS_GLOW_ROW_VERTICES), "complete glow row strip");

int sh4der_kos_glow_prepare(sh4der_kos_glow_stage_t *out,
    const uint16_t *grid, size_t count, const uint8_t lut[256],
    sh4der_kos_frame_id_t source, const sh4der_kos_glow_stage_t *previous,
    uint8_t keep) {
    if(!out || !grid || !lut || !source.generation || !source.render ||
       ((uintptr_t)out & 31u) || (keep && (!previous ||
       previous->source.generation != source.generation ||
       !previous->source.render || previous->source.render >= source.render))) {
        errno = EINVAL; return -1;
    }
    if(count < SH4DER_KOS_FRAME_SAMPLES) { errno = ENOSPC; return -1; }
    unsigned sum = 0;
    for(unsigned y = 0; y < SH4DER_GLOW_GRID_Y; ++y) {
        for(unsigned x = 0; x < SH4DER_GLOW_GRID_X; ++x) {
            unsigned i = y * SH4DER_GLOW_TEX_W + x;
            uint16_t pixel = sh4der_glow_texel(grid[y * SH4DER_GLOW_GRID_X + x], lut);
            if(keep) {
                unsigned old = previous->big[i], next = 256u - keep;
                unsigned r = ((old >> 11) * keep + (pixel >> 11) * next + 128u) >> 8;
                unsigned g = (((old >> 5) & 63u) * keep + ((pixel >> 5) & 63u) * next + 128u) >> 8;
                unsigned b = ((old & 31u) * keep + (pixel & 31u) * next + 128u) >> 8;
                pixel = (uint16_t)((r << 11) | (g << 5) | b);
            }
            sum += sh4der_glow_luma8_(pixel);
            out->big[i] = pixel;
        }
        memset(out->big + y * SH4DER_GLOW_TEX_W + SH4DER_GLOW_GRID_X, 0,
               (SH4DER_GLOW_TEX_W - SH4DER_GLOW_GRID_X) * sizeof(uint16_t));
    }
    memset(out->big + SH4DER_GLOW_GRID_Y * SH4DER_GLOW_TEX_W, 0,
           (SH4DER_GLOW_TEX_H - SH4DER_GLOW_GRID_Y) * SH4DER_GLOW_TEX_W * sizeof(uint16_t));
    memset(out->small, 0, sizeof(out->small));
    sh4der_glow_shrink4(out->big, SH4DER_GLOW_TEX_W,
                      out->small, SH4DER_GLOW_SMALL_TEX_W);
    out->mean = sh4der_div_((float)sum, 255.0f * SH4DER_KOS_FRAME_SAMPLES);
    out->source = source;
    return 0;
}

int sh4der_kos_glow_feedback_record(sh4der_kos_glow_feedback_t *out,
    const sh4der_kos_frame_metadata_t *m,const sh4der_kos_glow_stage_t *maps,
    float big,float small,float darken) {
    if(!out || !m || !maps || !m->id.generation || !m->id.render ||
       m->id.generation!=maps->source.generation || !maps->source.render ||
       maps->source.render>=m->id.render){errno=EINVAL;return -1;}
    float gain=m->gain*(1-darken);
    if(!isfinite(big) || big<0 || big>16 || !isfinite(small) || small<0 || small>16 ||
       !isfinite(darken) || darken<0 || darken>=1 || !isfinite(gain) || gain<=.00390625f || gain>256){errno=EDOM;return -1;}
    *out=(sh4der_kos_glow_feedback_t){.maps=maps,.rendered=m->id,.map_source=maps->source,
        .big=big,.small=small,.inverse_gain=1/gain};
    return 0;
}
int sh4der_kos_glow_feedback_remove(const sh4der_kos_glow_feedback_t *f,
    sh4der_kos_frame_id_t sampled,uint16_t *grid,size_t count) {
    if(!f || !f->maps || !grid){errno=EINVAL;return -1;}
    if(count<SH4DER_KOS_FRAME_SAMPLES){errno=ENOSPC;return -1;}
    if(sampled.generation!=f->rendered.generation || sampled.render!=f->rendered.render ||
       f->maps->source.generation!=f->map_source.generation || f->maps->source.render!=f->map_source.render){errno=ESTALE;return -1;}
    unsigned wb=(unsigned)(f->big*256),ws=(unsigned)(f->small*256);
    unsigned gain=(unsigned)(f->inverse_gain*256);
    for(unsigned y=0;y<SH4DER_GLOW_GRID_Y;++y)for(unsigned x=0;x<SH4DER_GLOW_GRID_X;++x) {
        unsigned p=grid[y*SH4DER_GLOW_GRID_X+x],m=f->maps->big[y*SH4DER_GLOW_TEX_W+x],
            s=f->maps->small[(y>>2)*SH4DER_GLOW_SMALL_TEX_W+(x>>2)];
        int r=(int)(p>>11)-(int)(((m>>11)*wb+(s>>11)*ws)>>8);
        int g=(int)((p>>5)&63)-(int)((((m>>5)&63)*wb+((s>>5)&63)*ws)>>8);
        int b=(int)(p&31)-(int)(((m&31)*wb+(s&31)*ws)>>8);
        unsigned rr=r>0?((unsigned)r*gain)>>8:0,gg=g>0?((unsigned)g*gain)>>8:0,bb=b>0?((unsigned)b*gain)>>8:0;
        grid[y*SH4DER_GLOW_GRID_X+x]=(uint16_t)((rr>31?31:rr)<<11|(gg>63?63:gg)<<5|(bb>31?31:bb));
    }
    return 0;
}

static int admit_map(const pvr_txr_surface_t *surface, uint32_t w, uint32_t h,
    pvr_ptr_t *address) {
    if(!surface || surface->width != w || surface->height != h ||
       surface->format != PVR_TXR_SURFACE_RGB565 ||
       surface->layout != PVR_TXR_SURFACE_LINEAR || surface->mipmapped) {
        errno = EINVAL; return -1;
    }
    if(pvr_txr_surface_get_texture_address(surface, address) < 0) return -1;
    if((uintptr_t)*address & 31u) { errno = EINVAL; return -1; }
    return 0;
}

int sh4der_kos_glow_bind(sh4der_kos_glow_binding_t *out,
    const pvr_txr_surface_t *big, const pvr_txr_surface_t *small) {
    pvr_ptr_t addresses[2];
    sh4der_kos_glow_binding_t next = {0};
    if(!out || ((uintptr_t)out & 31u)) { errno = EINVAL; return -1; }
    if(admit_map(big, SH4DER_GLOW_TEX_W, SH4DER_GLOW_TEX_H, &addresses[0]) < 0 ||
       admit_map(small, SH4DER_GLOW_SMALL_TEX_W, SH4DER_GLOW_SMALL_TEX_H, &addresses[1]) < 0)
        return -1;
    uintptr_t a = (uintptr_t)addresses[0], b = (uintptr_t)addresses[1];
    if(a < b + small->byte_size && b < a + big->byte_size) {
        errno = EINVAL; return -1;
    }
    for(unsigned i = 0; i < 3; ++i) {
        pvr_poly_cxt_t c;
        if(i) {
            const pvr_txr_surface_t *s = i == 1 ? big : small;
            pvr_poly_cxt_txr(&c, PVR_LIST_TR_POLY,
                PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                s->width, s->height, addresses[i - 1], PVR_FILTER_BILINEAR);
            c.txr.env = PVR_TXRENV_MODULATE;
            c.txr.uv_clamp = PVR_UVCLAMP_UV;
        }
        else pvr_poly_cxt_col(&c, PVR_LIST_TR_POLY);
        c.gen.culling = PVR_CULLING_NONE;
        c.gen.fog_type = PVR_FOG_DISABLE;
        c.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        c.depth.write = PVR_DEPTHWRITE_DISABLE;
        c.blend.src = i ? PVR_BLEND_ONE : PVR_BLEND_ZERO;
        c.blend.dst = i ? PVR_BLEND_ONE : PVR_BLEND_DESTCOLOR;
        pvr_poly_compile(&next.headers[i], &c);
    }
    next.big = big; next.small = small;
    *out = next;
    return 0;
}

int sh4der_kos_glow_upload(sh4der_kos_glow_binding_t *binding,
    const sh4der_kos_glow_stage_t *stage, const pvr_render_ticket_t *last_use,
    pvr_txr_transfer_t transfer) {
    if(!binding || !stage || !binding->big || !binding->small ||
       !stage->source.generation || !stage->source.render ||
       (transfer != PVR_TXR_TRANSFER_CPU && transfer != PVR_TXR_TRANSFER_SQ &&
        transfer != PVR_TXR_TRANSFER_DMA)) { errno = EINVAL; return -1; }
    if(last_use) {
        pvr_render_stage_t progress;
        if(pvr_render_ticket_get_stage(last_use, &progress) < 0) return -1;
        if(progress < PVR_RENDER_STAGE_COMPLETE) { errno = EAGAIN; return -1; }
    }
    binding->ready = false;
    if(pvr_txr_surface_upload(binding->big, stage->big, sizeof(stage->big), transfer) < 0 ||
       pvr_txr_surface_upload(binding->small, stage->small, sizeof(stage->small), transfer) < 0)
        return -1;
    binding->source = stage->source;
    binding->ready = true;
    return 0;
}

int sh4der_kos_glow_halo_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z0, float z_step, float strength, float radius,
    float darken, sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(!binding || !rect || !out || ((uintptr_t)out & 31u)) {
        errno = EINVAL; return -1;
    }
    if(!binding->ready) { errno = EAGAIN; return -1; }
    if(!isfinite(z0) || !isfinite(z_step) || z0 <= 0 || z_step <= 0 ||
       !isfinite(strength) || strength < 0 || strength > 1 ||
       !isfinite(radius) || radius < 0 || radius > 1 ||
       !isfinite(darken) || darken < 0 || darken > 1) {
        errno = EDOM; return -1;
    }
    for(unsigned i = 0; i < 4; ++i)
        if(!isfinite(rect[i])) { errno = EDOM; return -1; }
    if(rect[2] <= 0 || rect[3] <= 0 || !isfinite(rect[0] + rect[2]) ||
       !isfinite(rect[1] + rect[3])) { errno = EDOM; return -1; }
    float weights[3] = {darken, 0, 0}, depths[3];
    sh4der_glow_halo(strength, radius, &weights[1], &weights[2]);
    unsigned count = 0;
    float last = 0;
    for(unsigned i = 0; i < 3; ++i) {
        if(weights[i] <= 0) continue;
        depths[i] = z0 + i * z_step;
        if(!isfinite(depths[i]) || depths[i] <= last) { errno = ERANGE; return -1; }
        last = depths[i]; ++count;
    }
    if(capacity < count) { errno = ENOSPC; return -1; }
    /* Fixed map extents: bake texel-center UVs, not per-vertex divisions. */
    static const float uv[3][4] = {
        {0, 0, 0, 0},
        {0.5f / SH4DER_GLOW_TEX_W, (SH4DER_GLOW_GRID_X - 0.5f) / SH4DER_GLOW_TEX_W,
         0.5f / SH4DER_GLOW_TEX_H, (SH4DER_GLOW_GRID_Y - 0.5f) / SH4DER_GLOW_TEX_H},
        {0.5f / SH4DER_GLOW_SMALL_TEX_W, (SH4DER_GLOW_SMALL_X - 0.5f) / SH4DER_GLOW_SMALL_TEX_W,
         0.5f / SH4DER_GLOW_SMALL_TEX_H, (SH4DER_GLOW_SMALL_Y - 0.5f) / SH4DER_GLOW_SMALL_TEX_H}
    };
    unsigned n = 0;
    for(unsigned i = 0; i < 3; ++i) {
        if(weights[i] <= 0) continue;
        sh4der_kos_screen_packet_t *packet = &out[n++];
        packet->header = binding->headers[i];
        uint32_t color = sh4der_glow_argb(i ? weights[i] : 1.0f - darken);
        for(unsigned k = 0; k < 4; ++k) {
            float u = (k & 1) ? 1 : 0, v = (k & 2) ? 1 : 0;
            packet->vertices[k] = (pvr_vertex_t){
                .flags = k == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                .x = rect[0] + u * rect[2], .y = rect[1] + v * rect[3],
                .z = depths[i], .argb = color,
                .u = uv[i][k & 1], .v = uv[i][2 + ((k >> 1) & 1)]
            };
        }
    }
    return (int)n;
}

static int mesh_admit(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z, const sh4der_kos_glow_strip_t *out) {
    if(!binding || !rect || !out || ((uintptr_t)out & 31u)) {
        errno = EINVAL; return -1;
    }
    if(!binding->ready) { errno = EAGAIN; return -1; }
    if(!isfinite(z) || z <= 0) { errno = EDOM; return -1; }
    for(unsigned i = 0; i < 4; ++i)
        if(!isfinite(rect[i])) { errno = EDOM; return -1; }
    if(rect[2] <= 0 || rect[3] <= 0 || !isfinite(rect[0] + rect[2]) ||
       !isfinite(rect[1] + rect[3])) { errno = EDOM; return -1; }
    return 0;
}

static void mesh_copy(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z, const float anchor[2], float scale,
    unsigned ray, float sun_fade, float share, float flat,
    sh4der_kos_glow_strip_t *out) {
    for(unsigned y = 0; y < SH4DER_GLOW_MESH_Y; ++y) {
        out[y].header = binding->headers[1];
        for(unsigned x = 0; x <= SH4DER_GLOW_MESH_X; ++x)
            for(unsigned k = 0; k < 2; ++k) {
                float u = x * (1.0f / SH4DER_GLOW_MESH_X);
                float v = (y + k) * (1.0f / SH4DER_GLOW_MESH_Y);
                pvr_vertex_t *vertex = &out[y].vertices[x * 2 + k];
                float weight = ray ? sh4der_glow_ray_colour((int)ray,
                    anchor[0], anchor[1], u, v, sun_fade, share) : flat;
                *vertex = (pvr_vertex_t){
                    .flags = x == SH4DER_GLOW_MESH_X && k ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                    .x = rect[0] + u * rect[2], .y = rect[1] + v * rect[3],
                    .z = z, .argb = sh4der_glow_argb(weight)
                };
                sh4der_glow_ray_uv(scale, anchor[0], anchor[1], u, v,
                                  &vertex->u, &vertex->v);
            }
    }
}

int sh4der_kos_glow_ray_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z0, float z_step, const float sun[2],
    float share, unsigned rays, sh4der_kos_glow_strip_t *out, size_t capacity) {
    if(mesh_admit(binding, rect, z0, out) < 0) return -1;
    if(!sun || !rays || rays > SH4DER_KOS_GLOW_MAX_RAYS) { errno = EINVAL; return -1; }
    if(!isfinite(sun[0]) || !isfinite(sun[1]) || !isfinite(share) ||
       share < 0 || share > 1 || !isfinite(z_step) || z_step <= 0) {
        errno = EDOM; return -1;
    }
    const float edge_x = SH4DER_GLOW_SUN_EDGE / SH4DER_GLOW_ASPECT;
    if(share == 0 || sun[0] < -edge_x || sun[0] > 1 + edge_x ||
       sun[1] < -SH4DER_GLOW_SUN_EDGE || sun[1] > 1 + SH4DER_GLOW_SUN_EDGE)
        return 0;
    float fade = sh4der_glow_sun_fade(sun[0], sun[1]);
    if(fade <= 0) return 0;
    size_t count = rays * SH4DER_GLOW_MESH_Y;
    if(capacity < count) { errno = ENOSPC; return -1; }
    float depths[SH4DER_KOS_GLOW_MAX_RAYS];
    for(unsigned i = 0; i < rays; ++i) {
        depths[i] = z0 + i * z_step;
        if(!isfinite(depths[i]) || (i && depths[i] <= depths[i - 1])) {
            errno = ERANGE; return -1;
        }
    }
    for(unsigned i = 0; i < rays; ++i)
        mesh_copy(binding, rect, depths[i], sun, sh4der_glow_ray_scale((int)i + 1),
                  i + 1, fade, share, 0, out + i * SH4DER_GLOW_MESH_Y);
    return (int)count;
}

int sh4der_kos_glow_shaft_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z, const float anchor[2], float scale, float weight,
    sh4der_kos_glow_strip_t *out, size_t capacity) {
    if(mesh_admit(binding, rect, z, out) < 0) return -1;
    if(!anchor) { errno = EINVAL; return -1; }
    if(!isfinite(anchor[0]) || !isfinite(anchor[1]) ||
       anchor[0] < 0 || anchor[0] > 1 || anchor[1] < 0 || anchor[1] > 1 ||
       !isfinite(scale) || scale < 0 || scale > 1 ||
       !isfinite(weight) || weight < 0 || weight > 1) { errno = EDOM; return -1; }
    if(weight == 0) return 0;
    if(capacity < SH4DER_GLOW_MESH_Y) { errno = ENOSPC; return -1; }
    mesh_copy(binding, rect, z, anchor, scale, 0, 0, 0, weight, out);
    return SH4DER_GLOW_MESH_Y;
}
