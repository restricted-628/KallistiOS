/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos.h>
#include <errno.h>

int sh4der_kos_bind(sh4der_kos_binding_t *out,
                    const pvr_chunk_model_view_t *view,
                    const sh4der_kos_sidecars_t *sidecars) {
    if(!out || !view || !sidecars) {
        errno = EINVAL;
        return -1;
    }
    if((sidecars->holo_weights || sidecars->ao || sidecars->bones) &&
       view->info.vertex_entries &&
       sidecars->count <= view->info.maximum_vertex_index) {
        errno = ENOSPC;
        return -1;
    }
    *out = (sh4der_kos_binding_t){view, *sidecars};
    return 0;
}

static uint32_t compose(uint32_t base, uint32_t effect,
                        sh4der_kos_color_mode_t mode) {
    if(mode == SH4DER_KOS_REPLACE)
        return effect;
    uint32_t result = base & 0xff000000u;
    for(unsigned shift = 0; shift < 24; shift += 8) {
        unsigned b = (base >> shift) & 255, e = (effect >> shift) & 255;
        unsigned c = mode == SH4DER_KOS_MODULATE ? (b * e + 127) / 255 : b + e;
        result |= (c > 255 ? 255 : c) << shift;
    }
    return result;
}

int sh4der_kos_vertex(const pvr_chunk_render_state_t *state,
                      const pvr_chunk_vertex_attributes_t *attributes,
                      const pvr_chunk_strip_attributes_t *strip,
                      pvr_vertex_t *vertex, void *data) {
    const sh4der_kos_effect_t *e = data;
    const sh4der_kos_sidecars_t *s = &e->binding->sidecars;
    if(e->base_prepare) {
        int rv = e->base_prepare(state, attributes, strip, vertex, e->base_data);
        if(rv < 0)
            return rv;
    }
    unsigned index = attributes->index;
    uint32_t color = vertex->argb;
    if(e->blade) {
        const float xyz[3] = {vertex->x, vertex->y, vertex->z};
        uint32_t effect;
        sh4der_blade_argb(e->blade, e->blade_rgb, e->blade_alpha,
                          xyz, sizeof(xyz), &effect, sizeof(effect), 1);
        color = compose(color, effect, e->mode);
    }
    else if(e->holo)
        color = compose(color, sh4der_holo_argb(e->holo, s->holo_weights[index]), e->mode);
    if(!s->ao && !e->shadow) {
        vertex->argb = color;
        return 0;
    }
    float retained = s->ao ? (float)s->ao[index] / 255.0f : 1.0f;
    if(e->shadow)
        retained *= 1.0f - e->shadow->dark * sh4dow_strength(e->shadow, &e->shadow_axes,
            vertex->x, vertex->y, vertex->z,
            s->bones ? s->bones[index] : SH4DOW_BONE_NONE);
    uint32_t result = color & 0xff000000u;
    for(unsigned shift = 0; shift < 24; shift += 8)
        result |= (uint32_t)(((color >> shift) & 255) * retained + 0.5f) << shift;
    vertex->argb = result;
    return 0;
}
