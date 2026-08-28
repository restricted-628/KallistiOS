/* KallistiOS ##version##

   pvr_toon.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_toon.h>

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define BAND_POLYGON_CAPACITY 6u

static int finite_vector(const vector_t *value) {
    return value && isfinite(value->x) && isfinite(value->y) &&
           isfinite(value->z) && isfinite(value->w);
}

static int normalize_xyz(vector_t *output, const vector_t *input) {
    float length_squared;
    float reciprocal;

    if(!finite_vector(input)) {
        errno = EDOM;
        return -1;
    }
    length_squared = input->x * input->x + input->y * input->y +
                     input->z * input->z;
    if(!isfinite(length_squared) || length_squared <= FLT_MIN) {
        errno = EDOM;
        return -1;
    }
    reciprocal = 1.0f / sqrtf(length_squared);
    output->x = input->x * reciprocal;
    output->y = input->y * reciprocal;
    output->z = input->z * reciprocal;
    output->w = 0.0f;
    if(!isfinite(output->x) || !isfinite(output->y) ||
       !isfinite(output->z)) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

int pvr_toon_light_init(pvr_toon_light_t *light,
                        const vector_t *direction,
                        float intensity, float ambient) {
    pvr_toon_light_t candidate;

    if(!light || !direction) {
        errno = EINVAL;
        return -1;
    }
    if(!isfinite(intensity) || !isfinite(ambient)) {
        errno = EDOM;
        return -1;
    }
    if(normalize_xyz(&candidate.direction, direction) < 0)
        return -1;
    candidate.intensity = intensity;
    candidate.ambient = ambient;
    *light = candidate;
    return 0;
}

int pvr_toon_shade_evaluate(float *shade, const vector_t *normal,
                            const pvr_toon_light_t *light,
                            pvr_toon_shade_equation_t equation) {
    vector_t unit_normal;
    vector_t unit_light;
    float dot;
    float value;

    if(!shade || !light || !finite_vector(&light->direction) ||
       light->direction.w != 0.0f || !isfinite(light->intensity) ||
       !isfinite(light->ambient) ||
       equation < PVR_TOON_SHADE_DOT ||
       equation > PVR_TOON_SHADE_HALF_LAMBERT) {
        errno = EINVAL;
        return -1;
    }
    if(normalize_xyz(&unit_normal, normal) < 0 ||
       normalize_xyz(&unit_light, &light->direction) < 0)
        return -1;
    dot = unit_normal.x * unit_light.x + unit_normal.y * unit_light.y +
          unit_normal.z * unit_light.z;
    if(equation == PVR_TOON_SHADE_INVERTED_DOT)
        value = light->ambient - light->intensity * dot;
    else if(equation == PVR_TOON_SHADE_HALF_LAMBERT)
        value = light->ambient + light->intensity * (0.5f * dot + 0.5f);
    else
        value = light->ambient + light->intensity * dot;
    if(!isfinite(value)) {
        errno = ERANGE;
        return -1;
    }
    *shade = value;
    return 0;
}

static int thresholds_valid(const float *thresholds, size_t count,
                            float epsilon) {
    size_t index;

    if(!isfinite(epsilon) || epsilon < 0.0f || (count && !thresholds)) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < count; ++index) {
        if(!isfinite(thresholds[index])) {
            errno = EDOM;
            return -1;
        }
        if(index && (thresholds[index] <= thresholds[index - 1u] ||
           thresholds[index] - thresholds[index - 1u] <= 2.0f * epsilon)) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int pvr_toon_band_index(size_t *band, float shade,
                        const float *thresholds, size_t threshold_count) {
    size_t low = 0;
    size_t high = threshold_count;

    if(!band || !isfinite(shade)) {
        errno = EINVAL;
        return -1;
    }
    if(thresholds_valid(thresholds, threshold_count, 0.0f) < 0)
        return -1;
    while(low < high) {
        size_t middle = low + (high - low) / 2u;

        if(shade < thresholds[middle])
            high = middle;
        else
            low = middle + 1u;
    }
    *band = low;
    return 0;
}

int pvr_toon_triangle_capacity(size_t threshold_count, size_t *capacity) {
    size_t value;

    if(!capacity) {
        errno = EINVAL;
        return -1;
    }
    if(threshold_count > (SIZE_MAX - 1u) / 2u) {
        errno = ERANGE;
        return -1;
    }
    value = threshold_count * 2u + 1u;
    *capacity = value;
    return 0;
}

static int vertex_valid(const pvr_toon_vertex_t *vertex) {
    const float *values = (const float *)vertex;
    size_t float_count = offsetof(pvr_toon_vertex_t, argb) / sizeof(float);
    size_t index;

    for(index = 0; index < float_count; ++index) {
        if(!isfinite(values[index])) {
            errno = EDOM;
            return -1;
        }
    }
    if(vertex->position.w != 1.0f || vertex->normal.w != 0.0f) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static uint32_t color_lerp(uint32_t low, uint32_t high, float amount) {
    uint32_t output = 0;
    size_t shift;

    for(shift = 0; shift < 32u; shift += 8u) {
        float a = (float)((low >> shift) & UINT32_C(0xff));
        float b = (float)((high >> shift) & UINT32_C(0xff));
        uint32_t channel = (uint32_t)floorf(a + (b - a) * amount + 0.5f);

        if(channel > UINT32_C(0xff))
            channel = UINT32_C(0xff);
        output |= channel << shift;
    }
    return output;
}

static int vertex_interpolate(pvr_toon_vertex_t *output,
                              const pvr_toon_vertex_t *first,
                              const pvr_toon_vertex_t *second,
                              float threshold) {
    const pvr_toon_vertex_t *low = first;
    const pvr_toon_vertex_t *high = second;
    pvr_toon_vertex_t result;
    float amount;

    /* Ordering by the scalar makes independently traversed shared edges use
       identical arithmetic instead of mirrored t and 1-t expressions. */
    if(low->shade > high->shade) {
        low = second;
        high = first;
    }
    if(!(low->shade < threshold && high->shade > threshold)) {
        errno = EILSEQ;
        return -1;
    }
    amount = (threshold - low->shade) / (high->shade - low->shade);
    if(!isfinite(amount) || amount <= 0.0f || amount >= 1.0f) {
        errno = ERANGE;
        return -1;
    }

#define LERP_FIELD(field) \
    result.field = low->field + (high->field - low->field) * amount
    LERP_FIELD(position.x);
    LERP_FIELD(position.y);
    LERP_FIELD(position.z);
    LERP_FIELD(position.w);
    LERP_FIELD(normal.x);
    LERP_FIELD(normal.y);
    LERP_FIELD(normal.z);
    result.normal.w = 0.0f;
    LERP_FIELD(u);
    LERP_FIELD(v);
#undef LERP_FIELD
    result.shade = threshold;
    result.argb = color_lerp(low->argb, high->argb, amount);
    result.oargb = color_lerp(low->oargb, high->oargb, amount);
    result.source_index = PVR_TOON_GENERATED_INDEX;
    if(normalize_xyz(&result.normal, &result.normal) < 0)
        return -1;
    if(!isfinite(result.position.x) || !isfinite(result.position.y) ||
       !isfinite(result.position.z) || result.position.w != 1.0f ||
       !isfinite(result.u) || !isfinite(result.v)) {
        errno = ERANGE;
        return -1;
    }
    *output = result;
    return 0;
}

static int clip_band_plane(pvr_toon_vertex_t output[BAND_POLYGON_CAPACITY],
                           const pvr_toon_vertex_t *input,
                           size_t input_count, float threshold, int lower,
                           size_t *output_count) {
    size_t produced = 0;
    size_t index;

    for(index = 0; index < input_count; ++index) {
        const pvr_toon_vertex_t *current = input + index;
        const pvr_toon_vertex_t *previous =
            input + (index ? index - 1u : input_count - 1u);
        int current_inside = lower ? current->shade >= threshold :
                                     current->shade <= threshold;
        int previous_inside = lower ? previous->shade >= threshold :
                                      previous->shade <= threshold;
        int current_strict = lower ? current->shade > threshold :
                                     current->shade < threshold;
        int previous_strict = lower ? previous->shade > threshold :
                                      previous->shade < threshold;

        if((!previous_inside && current_strict) ||
           (previous_strict && !current_inside)) {
            if(produced >= BAND_POLYGON_CAPACITY ||
               vertex_interpolate(output + produced, previous, current,
                                  threshold) < 0)
                return -1;
            ++produced;
        }
        if(current_inside) {
            if(produced >= BAND_POLYGON_CAPACITY) {
                errno = EOVERFLOW;
                return -1;
            }
            output[produced++] = *current;
        }
    }
    *output_count = produced;
    return 0;
}

static int band_polygon(const pvr_toon_vertex_t input[3], size_t band,
                        const float *thresholds, size_t threshold_count,
                        pvr_toon_vertex_t output[BAND_POLYGON_CAPACITY],
                        size_t *output_count) {
    pvr_toon_vertex_t scratch[BAND_POLYGON_CAPACITY];
    size_t count = 3;

    memcpy(scratch, input, sizeof(*input) * 3u);
    if(band) {
        if(clip_band_plane(output, scratch, count, thresholds[band - 1u],
                           1, &count) < 0)
            return -1;
        memcpy(scratch, output, count * sizeof(*output));
    }
    if(band < threshold_count) {
        size_t index;
        int all_on_upper = count != 0;

        for(index = 0; index < count; ++index) {
            if(scratch[index].shade != thresholds[band]) {
                all_on_upper = 0;
                break;
            }
        }
        /* Exact-boundary geometry belongs to the higher band. Keeping the
           boundary on both sides is required for crossing polygons, but a
           whole polygon on the plane must not be emitted twice. */
        if(all_on_upper) {
            *output_count = 0;
            return 0;
        }
        if(clip_band_plane(output, scratch, count, thresholds[band],
                           0, &count) < 0)
            return -1;
    }
    else {
        memcpy(output, scratch, count * sizeof(*output));
    }
    *output_count = count;
    return 0;
}

static int partition_pass(pvr_toon_triangle_t *output,
                          const pvr_toon_vertex_t input[3],
                          const float *thresholds, size_t threshold_count,
                          pvr_toon_split_result_t *progress) {
    size_t band;

    for(band = 0; band <= threshold_count; ++band) {
        pvr_toon_vertex_t polygon[BAND_POLYGON_CAPACITY];
        size_t polygon_count;
        size_t triangle;

        if(band_polygon(input, band, thresholds, threshold_count,
                        polygon, &polygon_count) < 0)
            return -1;
        if(polygon_count < 3u)
            continue;
        for(triangle = 1u; triangle + 1u < polygon_count; ++triangle) {
            pvr_toon_triangle_t *destination = output ?
                output + progress->output_triangles : NULL;
            const size_t indices[3] = { 0, triangle, triangle + 1u };
            size_t corner;

            if(destination) {
                destination->band = band;
                for(corner = 0; corner < 3u; ++corner)
                    destination->vertices[corner] = polygon[indices[corner]];
            }
            for(corner = 0; corner < 3u; ++corner) {
                if(polygon[indices[corner]].source_index ==
                   PVR_TOON_GENERATED_INDEX)
                    ++progress->generated_vertices;
            }
            ++progress->output_triangles;
        }
    }
    return 0;
}

int pvr_toon_split_triangle(
    pvr_toon_triangle_t *output, size_t output_capacity,
    const pvr_toon_vertex_t input[3],
    const float *thresholds, size_t threshold_count, float epsilon,
    pvr_toon_split_result_t *result) {
    pvr_toon_split_result_t required = { 0 };
    pvr_toon_vertex_t snapped[3];
    size_t vertex;
    size_t threshold;
    float minimum;
    float maximum;

    if(result)
        memset(result, 0, sizeof(*result));
    if(!input || (output_capacity && !output)) {
        errno = EINVAL;
        return -1;
    }
    if(thresholds_valid(thresholds, threshold_count, epsilon) < 0)
        return -1;
    memcpy(snapped, input, sizeof(snapped));
    for(vertex = 0; vertex < 3u; ++vertex) {
        if(vertex_valid(snapped + vertex) < 0 ||
           normalize_xyz(&snapped[vertex].normal,
                         &snapped[vertex].normal) < 0)
            return -1;
        for(threshold = 0; threshold < threshold_count; ++threshold) {
            if(fabsf(snapped[vertex].shade - thresholds[threshold]) <=
               epsilon) {
                snapped[vertex].shade = thresholds[threshold];
                break;
            }
        }
    }
    minimum = maximum = snapped[0].shade;
    for(vertex = 1; vertex < 3u; ++vertex) {
        if(snapped[vertex].shade < minimum)
            minimum = snapped[vertex].shade;
        if(snapped[vertex].shade > maximum)
            maximum = snapped[vertex].shade;
    }
    for(threshold = 0; threshold < threshold_count; ++threshold) {
        if(thresholds[threshold] > minimum &&
           thresholds[threshold] < maximum)
            ++required.crossed_thresholds;
    }
    if(partition_pass(NULL, snapped, thresholds, threshold_count,
                      &required) < 0)
        return -1;
    if(output_capacity < required.output_triangles) {
        if(result)
            *result = required;
        errno = ENOSPC;
        return -1;
    }

    required.output_triangles = 0;
    required.generated_vertices = 0;
    if(partition_pass(output, snapped, thresholds, threshold_count,
                      &required) < 0)
        return -1;
    if(result)
        *result = required;
    return 0;
}

int pvr_toon_color_modulate(uint32_t *output, uint32_t base,
                            uint32_t modulation) {
    uint32_t result = 0;
    size_t shift;

    if(!output) {
        errno = EINVAL;
        return -1;
    }
    for(shift = 0; shift < 32u; shift += 8u) {
        uint32_t a = (base >> shift) & UINT32_C(0xff);
        uint32_t b = (modulation >> shift) & UINT32_C(0xff);
        uint32_t product = (a * b + UINT32_C(127)) / UINT32_C(255);

        result |= product << shift;
    }
    *output = result;
    return 0;
}
