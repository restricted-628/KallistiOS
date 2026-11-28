#include <sh4der_kos_routed_lighting.h>

#include <errno.h>
#include <math.h>

int sh4der_kos_routed_lighting_prepare(
    sh4der_kos_routed_lighting_plan_t *plan,
    const sh4der_kos_routed_directional_light_t *lights,
    unsigned count, const sh4der_kos_routed_masks_t masks,
    pvr_geometry_vertex_format_t format,
    const sh4der_kos_routed_ambient_t *ambient) {
    if(!plan || !masks || count > SH4DER_KOS_ROUTED_LIGHT_MAX || (count && !lights)) {
        errno = EINVAL;
        return -1;
    }

    if((format != PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR &&
        format != PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED) ||
       (format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR &&
        (masks[0][SH4DER_KOS_ROUTED_OFFSET] || masks[1][SH4DER_KOS_ROUTED_OFFSET]))) {
        errno = EINVAL;
        return -1;
    }
    unsigned active = 0;
    for(unsigned volume = 0; volume < SH4DER_KOS_ROUTED_VOLUMES; ++volume)
        for(unsigned field = 0; field < SH4DER_KOS_ROUTED_FIELDS; ++field)
            active |= masks[volume][field];
    if(active >= (1u << count)) {
        errno = EINVAL;
        return -1;
    }

    sh4der_kos_routed_lighting_plan_t prepared = {0};
    if(ambient) {
        for(unsigned volume = 0; volume < SH4DER_KOS_ROUTED_VOLUMES; ++volume) {
            const uint32_t *rgb = volume && ambient->separate_inside ?
                                  ambient->inside : ambient->outside;
            if((rgb[SH4DER_KOS_ROUTED_BASE] | rgb[SH4DER_KOS_ROUTED_OFFSET]) > 0xffffffu ||
               (format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR && rgb[SH4DER_KOS_ROUTED_OFFSET])) {
                errno = EINVAL;
                return -1;
            }
            for(unsigned field = 0; field < SH4DER_KOS_ROUTED_FIELDS; ++field) {
                for(unsigned channel = 0; channel < SH4DER_KOS_ROUTED_CHANNELS; ++channel) {
                    unsigned shift = 16u - 8u * channel;
                    prepared.ambient[volume][field][channel] =
                        (float)((rgb[field] >> shift) & 255u) / 255.0f;
                }
            }
        }
    }
    for(unsigned i = 0; i < count; ++i) {
        if(!(active & (1u << i)))
            continue;
        const sh4der_kos_routed_directional_light_t *light = &lights[i];
        const float length2 = shz_vec3_dot(light->direction, light->direction);
        if(!(fabsf(length2 - 1.0f) <= 0.001f && fabsf(light->gain) <= 1.0f)) {
            errno = EDOM;
            return -1;
        }
        for(unsigned channel = 0; channel < SH4DER_KOS_ROUTED_CHANNELS; ++channel) {
            if(!(light->color[channel] >= 0 && light->color[channel] <= 1)) {
                errno = EDOM;
                return -1;
            }
        }

        const unsigned lane = prepared.count++;
        prepared.directions[lane] = light->direction;
        prepared.thin |= (unsigned)light->thin << lane;
        for(unsigned volume = 0; volume < SH4DER_KOS_ROUTED_VOLUMES; ++volume) {
            for(unsigned field = 0; field < SH4DER_KOS_ROUTED_FIELDS; ++field) {
                if(masks[volume][field] & (1u << i)) {
                    for(unsigned channel = 0; channel < SH4DER_KOS_ROUTED_CHANNELS; ++channel)
                        prepared.weights[volume][field][channel][lane] =
                            light->gain * light->color[channel];
                }
            }
        }
    }
    *plan = prepared;
    return 0;
}

static unsigned pack_channel(float value) {
    return (unsigned)(shz_clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

int sh4der_kos_routed_lighting_vertex(
    const pvr_chunk_render_state_t *state,
    uint16_t source_index,
    const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    (void)state;
    (void)source_index;
    const sh4der_kos_routed_lighting_plan_t *plan = data;
    const vector_t *n = &deformation->normal;
    const shz_vec3_t normal = shz_vec3_init(n->x, n->y, n->z);
    float incidence[SH4DER_KOS_ROUTED_LIGHT_MAX] = {0};

    switch(plan->count) {
        case 3: {
            const shz_vec3_t dots = shz_vec3_dot3(normal, plan->directions[0],
                                                 plan->directions[1], plan->directions[2]);
            incidence[2] = dots.z;
            incidence[1] = dots.y;
            incidence[0] = dots.x;
            break;
        }
        case 2: {
            const shz_vec2_t dots = shz_vec3_dot2(normal, plan->directions[0],
                                                 plan->directions[1]);
            incidence[1] = dots.y;
            incidence[0] = dots.x;
            break;
        }
        case 1:
            incidence[0] = shz_vec3_dot(normal, plan->directions[0]);
            break;
    }
    for(unsigned i = 0; i < plan->count; ++i) {
        if(incidence[i] < 0.0f)
            incidence[i] = (plan->thin & (1u << i)) ? -incidence[i] : 0.0f;
    }

    uint32_t packed[SH4DER_KOS_ROUTED_VOLUMES][SH4DER_KOS_ROUTED_FIELDS];
    const unsigned fields = format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED ?
                            SH4DER_KOS_ROUTED_FIELDS : 1;
    for(unsigned volume = 0; volume < SH4DER_KOS_ROUTED_VOLUMES; ++volume) {
        for(unsigned field = 0; field < fields; ++field) {
            uint32_t rgb = 0;
            for(unsigned channel = 0; channel < SH4DER_KOS_ROUTED_CHANNELS; ++channel) {
                const float *weights = plan->weights[volume][field][channel];
                const float value = plan->ambient[volume][field][channel] +
                                    incidence[0] * weights[0] + incidence[1] * weights[1] +
                                    incidence[2] * weights[2];
                rgb = (rgb << 8) | pack_channel(value);
            }
            packed[volume][field] = rgb;
        }
    }

    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED) {
        vertex->textured.argb0 = (vertex->textured.argb0 & 0xff000000u) | packed[0][SH4DER_KOS_ROUTED_BASE];
        vertex->textured.argb1 = (vertex->textured.argb1 & 0xff000000u) | packed[1][SH4DER_KOS_ROUTED_BASE];
        vertex->textured.oargb0 = (vertex->textured.oargb0 & 0xff000000u) | packed[0][SH4DER_KOS_ROUTED_OFFSET];
        vertex->textured.oargb1 = (vertex->textured.oargb1 & 0xff000000u) | packed[1][SH4DER_KOS_ROUTED_OFFSET];
    }
    else {
        vertex->color.argb0 = (vertex->color.argb0 & 0xff000000u) | packed[0][SH4DER_KOS_ROUTED_BASE];
        vertex->color.argb1 = (vertex->color.argb1 & 0xff000000u) | packed[1][SH4DER_KOS_ROUTED_BASE];
    }
    return 0;
}
