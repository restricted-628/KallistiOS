#include <sh4der_kos_spotlight.h>

#include <errno.h>
#include <math.h>

int sh4der_kos_spotlight_prepare(sh4der_kos_spotlight_plan_t *out,
                                 const sh4der_kos_spotlight_t *light) {
    if(!out || !light || light->rgb > 0xffffffu) {
        errno = EINVAL;
        return -1;
    }

    const float radius_squared = light->radius * light->radius;
    const float axis_squared = shz_vec3_dot(light->axis, light->axis);
    if(!isfinite(light->position.x) || !isfinite(light->position.y) ||
       !isfinite(light->position.z) || !(fabsf(axis_squared - 1.0f) <= 0.001f) ||
       !(light->cutoff_cos >= 0 && light->cutoff_cos < 1) ||
       !(light->radius > 0 && radius_squared > 0 && isfinite(radius_squared)) ||
       !(light->constant > 0 && isfinite(light->constant)) ||
       !(light->linear >= 0 && isfinite(light->linear)) ||
       !(light->quadratic >= 0 && isfinite(light->quadratic))) {
        errno = EDOM;
        return -1;
    }

    *out = (sh4der_kos_spotlight_plan_t){
        .light = *light, .radius_squared = radius_squared,
        .inverse_cone_width = 1.0f / (1.0f - light->cutoff_cos),
        .tint = {((light->rgb >> 16) & 255u) / 255.0f,
                 ((light->rgb >> 8) & 255u) / 255.0f,
                 (light->rgb & 255u) / 255.0f}
    };
    return 0;
}

static float spot_strength(const sh4der_kos_spotlight_plan_t *plan,
                           const pvr_deform_vertex_t *v) {
    const sh4der_kos_spotlight_t *light = &plan->light;
    shz_vec3_t p = shz_vec3_init(v->position.x, v->position.y, v->position.z);
    shz_vec3_t delta = shz_vec3_sub(p, light->position);
    float d2 = shz_vec3_dot(delta, delta);
    if(!(d2 > 0 && d2 <= plan->radius_squared))
        return 0;

    float inv_distance = shz_inv_sqrtf_fsrra(d2);
    shz_vec3_t ray = shz_vec3_scale(delta, inv_distance);
    shz_vec3_t normal = shz_vec3_init(v->normal.x, v->normal.y, v->normal.z);
    shz_vec2_t dots = shz_vec3_dot2(ray, light->axis, normal);
    if(dots.x <= light->cutoff_cos || dots.y >= 0)
        return 0;

    float cone = shz_fminf(1.0f, (dots.x - light->cutoff_cos) * plan->inverse_cone_width);
    float attenuation = light->constant + light->linear * (d2 * inv_distance)
                      + light->quadratic * d2;
    return shz_fminf(1.0f, (cone * shz_fminf(1.0f, -dots.y)) / attenuation);
}

int sh4der_kos_spotlight_vertex(const pvr_chunk_render_state_t *state,
    uint16_t source_index, const pvr_deform_vertex_t *deformation,
    pvr_vertex_t *vertex, void *data) {
    (void)state;
    (void)source_index;
    const sh4der_kos_spotlight_plan_t *plan = data;
    float strength = spot_strength(plan, deformation);
    uint32_t color = vertex->argb;
    uint32_t lit = color & 0xff000000u;
    for(unsigned i = 0; i < 3; ++i) {
        unsigned shift = 16u - 8u * i;
        float channel = (float)((color >> shift) & 255u);
        lit |= (uint32_t)(channel * plan->tint[i] * strength + 0.5f) << shift;
    }
    vertex->argb = lit;
    return 0;
}
