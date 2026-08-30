/* KallistiOS ##version##

   pvr_cell.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_cell.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PVR_CELL_FLAGS_ALL \
    (PVR_CELL_FLIP_U | PVR_CELL_FLIP_V | PVR_CELL_HIDDEN)

_Static_assert(offsetof(pvr_cell_resolved_t, instance) == 0,
               "resolved cell must begin with sprite instance");

static int finite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int range_valid(uintptr_t address, size_t count, size_t size) {
    size_t bytes;

    if(count > SIZE_MAX / size)
        return 0;
    bytes = count * size;
    return bytes <= UINTPTR_MAX - address;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs_size && rhs_size && lhs < rhs + rhs_size &&
           rhs < lhs + lhs_size;
}

static int state_fields_valid(const pvr_cell_state_t *state,
                              uint32_t fields) {
    if((fields & PVR_CELL_KEY_OFFSET) &&
       !finite3(state->offset.x, state->offset.y, state->offset.z))
        return 0;
    if((fields & PVR_CELL_KEY_ROTATION) && !isfinite(state->rotation))
        return 0;
    if((fields & PVR_CELL_KEY_SCALE) &&
       (!isfinite(state->scale_x) || !isfinite(state->scale_y) ||
        state->scale_x <= 0.0f || state->scale_y <= 0.0f))
        return 0;
    if((fields & PVR_CELL_KEY_FLAGS) &&
       (state->flags & ~PVR_CELL_FLAGS_ALL))
        return 0;
    return 1;
}

static int state_valid(const pvr_cell_state_t *state) {
    return state && state_fields_valid(state, PVR_CELL_KEY_ALL);
}

static int sprite_valid(const pvr_cell_sprite_t *sprite) {
    size_t i;

    if(!sprite || !sprite->base_cells || !sprite->cell_count ||
       ((uintptr_t)sprite->base_cells &
        (_Alignof(pvr_cell_state_t) - 1u)) ||
       !range_valid((uintptr_t)sprite->base_cells, sprite->cell_count,
                    sizeof(*sprite->base_cells)) ||
       !finite3(sprite->position.x, sprite->position.y,
                sprite->position.z) ||
       !isfinite(sprite->rotation) || !isfinite(sprite->scale_x) ||
       !isfinite(sprite->scale_y) || sprite->scale_x <= 0.0f ||
       sprite->scale_y <= 0.0f)
        return 0;

    for(i = 0; i < sprite->cell_count; ++i) {
        if(!state_valid(&sprite->base_cells[i]))
            return 0;
    }
    return 1;
}

static int stream_view_valid(const pvr_cell_stream_view_t *view) {
    const pvr_cell_stream_t *stream;

    if(!view || !view->slot_count)
        return 0;
    stream = &view->stream;
    return (!stream->key_count || stream->keys) &&
           (!stream->keys ||
            !((uintptr_t)stream->keys &
              (_Alignof(pvr_cell_key_t) - 1u))) &&
           range_valid((uintptr_t)stream->keys, stream->key_count,
                       sizeof(*stream->keys)) &&
           isfinite(stream->time_offset) && isfinite(stream->time_max) &&
           stream->time_max > 0.0f && stream->repeat <= 1u;
}

static int key_valid(const pvr_cell_key_t *key, size_t slot_count,
                     float time_max, uint32_t repeat) {
    return key && isfinite(key->time) && key->time >= 0.0f &&
           (repeat ? key->time < time_max : key->time <= time_max) &&
           key->slot_index < slot_count &&
           key->fields && !(key->fields & ~PVR_CELL_KEY_ALL) &&
           state_fields_valid(&key->value, key->fields);
}

int pvr_cell_stream_open(const pvr_cell_stream_t *stream, size_t slot_count,
                         pvr_cell_stream_view_t *output) {
    pvr_cell_stream_view_t view;
    size_t i;

    if(!stream || !output || !slot_count ||
       (stream->key_count && !stream->keys) ||
       (stream->keys &&
        ((uintptr_t)stream->keys & (_Alignof(pvr_cell_key_t) - 1u))) ||
       !range_valid((uintptr_t)stream->keys, stream->key_count,
                    sizeof(*stream->keys)) ||
       !isfinite(stream->time_offset) || !isfinite(stream->time_max) ||
       stream->time_max <= 0.0f || stream->repeat > 1u) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < stream->key_count; ++i) {
        if(!key_valid(&stream->keys[i], slot_count, stream->time_max,
                      stream->repeat) ||
           (i && stream->keys[i].time < stream->keys[i - 1u].time)) {
            errno = EINVAL;
            return -1;
        }
    }

    view.stream = *stream;
    view.slot_count = slot_count;
    *output = view;
    return 0;
}

static float stream_time_map(const pvr_cell_stream_t *stream, float time) {
    double shifted = (double)time + stream->time_offset;
    double maximum = stream->time_max;

    if(stream->repeat) {
        double mapped = fmod(shifted, maximum);

        if(mapped < 0.0)
            mapped += maximum;
        return (float)mapped;
    }
    if(shifted <= 0.0)
        return 0.0f;
    if(shifted >= maximum)
        return stream->time_max;
    return (float)shifted;
}

static size_t stream_key_upper_bound(const pvr_cell_stream_t *stream,
                                     float time) {
    size_t low = 0;
    size_t high = stream->key_count;

    while(low < high) {
        size_t middle = low + (high - low) / 2u;

        if(stream->keys[middle].time <= time)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static void state_apply(pvr_cell_state_t *state,
                        const pvr_cell_key_t *key) {
    uint32_t fields = key->fields;

    if(fields & PVR_CELL_KEY_ATLAS_CELL)
        state->atlas_cell_index = key->value.atlas_cell_index;
    if(fields & PVR_CELL_KEY_OFFSET)
        state->offset = key->value.offset;
    if(fields & PVR_CELL_KEY_ROTATION)
        state->rotation = key->value.rotation;
    if(fields & PVR_CELL_KEY_SCALE) {
        state->scale_x = key->value.scale_x;
        state->scale_y = key->value.scale_y;
    }
    if(fields & PVR_CELL_KEY_PRIORITY)
        state->priority = key->value.priority;
    if(fields & PVR_CELL_KEY_FLAGS)
        state->flags = key->value.flags;
    if(fields & PVR_CELL_KEY_MATERIAL)
        state->material_id = key->value.material_id;
    if(fields & PVR_CELL_KEY_DIFFUSE)
        memcpy(state->argb, key->value.argb, sizeof(state->argb));
    if(fields & PVR_CELL_KEY_SPECULAR)
        memcpy(state->oargb, key->value.oargb, sizeof(state->oargb));
}

int pvr_cell_stream_sample(const pvr_cell_stream_view_t *stream, float time,
                           pvr_cell_state_t *cells, size_t cell_count,
                           size_t *applied_keys) {
    float mapped_time;
    size_t applied;
    size_t i;

    if(!stream_view_valid(stream) || !isfinite(time) || !cells ||
       cell_count != stream->slot_count ||
       ((uintptr_t)cells & (_Alignof(pvr_cell_state_t) - 1u)) ||
       !range_valid((uintptr_t)cells, cell_count, sizeof(*cells))) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(!state_valid(&cells[i])) {
            errno = EINVAL;
            return -1;
        }
    }

    mapped_time = stream_time_map(&stream->stream, time);
    applied = stream_key_upper_bound(&stream->stream, mapped_time);
    for(i = 0; i < applied; ++i)
        state_apply(&cells[stream->stream.keys[i].slot_index],
                    &stream->stream.keys[i]);
    if(applied_keys)
        *applied_keys = applied;
    return 0;
}

int pvr_cell_stream_list_sample(const pvr_cell_sprite_t *sprite,
                                const pvr_cell_stream_list_t *streams,
                                float time,
                                pvr_cell_state_t *output,
                                pvr_cell_state_t *workspace,
                                size_t capacity,
                                pvr_cell_sample_result_t *result) {
    pvr_cell_sample_result_t sampled = { 0, 0, 0 };
    size_t bytes;
    size_t i;

    if(result)
        *result = sampled;
    if(!sprite_valid(sprite) || !streams || !isfinite(time) || !output ||
       !workspace || capacity < sprite->cell_count ||
       ((uintptr_t)output & (_Alignof(pvr_cell_state_t) - 1u)) ||
       ((uintptr_t)workspace & (_Alignof(pvr_cell_state_t) - 1u)) ||
       (streams->stream_count && !streams->streams) ||
       (streams->streams &&
        ((uintptr_t)streams->streams &
         (_Alignof(pvr_cell_stream_view_t) - 1u))) ||
       !range_valid((uintptr_t)streams->streams, streams->stream_count,
                    sizeof(*streams->streams)) ||
       !range_valid((uintptr_t)output, capacity, sizeof(*output)) ||
       !range_valid((uintptr_t)workspace, capacity, sizeof(*workspace))) {
        errno = EINVAL;
        return -1;
    }

    bytes = sprite->cell_count * sizeof(*output);
    if(ranges_overlap((uintptr_t)output, bytes,
                      (uintptr_t)workspace, bytes) ||
       ranges_overlap((uintptr_t)output, bytes,
                      (uintptr_t)sprite->base_cells, bytes) ||
       ranges_overlap((uintptr_t)workspace, bytes,
                      (uintptr_t)sprite->base_cells, bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < streams->stream_count; ++i) {
        if(!stream_view_valid(&streams->streams[i]) ||
           streams->streams[i].slot_count != sprite->cell_count) {
            errno = EINVAL;
            return -1;
        }
    }

    memcpy(workspace, sprite->base_cells, bytes);
    for(i = 0; i < streams->stream_count; ++i) {
        size_t applied;

        if(pvr_cell_stream_sample(&streams->streams[i], time, workspace,
                                  sprite->cell_count, &applied) < 0)
            return -1;
        if(SIZE_MAX - sampled.applied_keys < applied) {
            errno = ERANGE;
            return -1;
        }
        sampled.applied_keys += applied;
        ++sampled.sampled_streams;
    }

    memcpy(output, workspace, bytes);
    sampled.published_cells = sprite->cell_count;
    if(result)
        *result = sampled;
    return 0;
}

static int cell_event_view_valid(const anim_event_track_view_t *view,
                                 const pvr_cell_stream_t *stream) {
    const anim_event_track_t *track;
    size_t i;

    if(!view)
        return 0;
    track = &view->track;
    if(!track->events || !track->event_count ||
       ((uintptr_t)track->events &
        (_Alignof(anim_event_key_t) - 1u)) ||
       !range_valid((uintptr_t)track->events, track->event_count,
                    sizeof(*track->events)) ||
       !isfinite(view->start_time) || !isfinite(view->end_time) ||
       track->events[0].time != view->start_time ||
       track->events[track->event_count - 1u].time != view->end_time)
        return 0;

    for(i = 0; i < track->event_count; ++i) {
        float time = track->events[i].time;

        if(!isfinite(time) || time < 0.0f ||
           (stream->repeat ? time >= stream->time_max :
                             time > stream->time_max) ||
           (i && time <= track->events[i - 1u].time))
            return 0;
    }
    return 1;
}

static int event_total_add(uint64_t *total, double count) {
    const double first_unrepresentable = 18446744073709551616.0;

    if(!isfinite(count) || count < 0.0 ||
       count >= first_unrepresentable ||
       count > (double)(UINT64_MAX - *total)) {
        errno = ERANGE;
        return -1;
    }
    *total += (uint64_t)count;
    return 0;
}

int pvr_cell_stream_collect_events(
        const pvr_cell_stream_view_t *stream,
        float previous_time, float current_time,
        const anim_event_track_view_t *events,
        anim_event_occurrence_t *output, size_t output_capacity,
        anim_event_result_t *result) {
    anim_event_result_t collected = { 0, 0, false };
    const anim_event_track_t *track;
    double shifted_previous;
    double shifted_current;
    size_t published = 0;
    size_t i;

    if(result)
        *result = collected;
    if(!stream_view_valid(stream) || !isfinite(previous_time) ||
       !isfinite(current_time) || current_time < previous_time ||
       !cell_event_view_valid(events, &stream->stream) ||
       (output_capacity && !output) ||
       (output &&
        ((uintptr_t)output &
         (_Alignof(anim_event_occurrence_t) - 1u))) ||
       !range_valid((uintptr_t)output, output_capacity, sizeof(*output))) {
        errno = EINVAL;
        return -1;
    }

    track = &events->track;
    shifted_previous = (double)previous_time + stream->stream.time_offset;
    shifted_current = (double)current_time + stream->stream.time_offset;

    if(!stream->stream.repeat) {
        for(i = 0; i < track->event_count; ++i) {
            const anim_event_key_t *event = &track->events[i];

            /* Compare on the unclamped local timeline so an event at zero is
               observed when a positive stream offset brings the application
               interval across the stream's starting boundary. */
            if(event->time > shifted_previous &&
               event->time <= shifted_current) {
                if(collected.matching_events == UINT64_MAX) {
                    errno = ERANGE;
                    return -1;
                }
                ++collected.matching_events;
                if(published < output_capacity) {
                    output[published].event = *event;
                    output[published].direction = ANIM_PLAYBACK_FORWARD;
                    ++published;
                }
            }
        }
    }
    else {
        const double exact_integer_limit = 9007199254740991.0;
        double period = stream->stream.time_max;
        double cursor = shifted_previous;

        for(i = 0; i < track->event_count; ++i) {
            double event_time = track->events[i].time;
            double first = floor((shifted_previous - event_time) / period);
            double last = floor((shifted_current - event_time) / period);

            /* Above 2^53, a double cannot distinguish adjacent cycle indices,
               so an exact event count or chronological occurrence is no
               longer representable from the float-based public timeline. */
            if(fabs(first) > exact_integer_limit ||
               fabs(last) > exact_integer_limit) {
                errno = ERANGE;
                return -1;
            }
            if(event_total_add(&collected.matching_events,
                               last - first) < 0)
                return -1;
        }

        /* Select the next occurrence from each event's arithmetic sequence.
           Work is bounded by output capacity times event count even if an
           application interval crosses millions of cycles. */
        while(published < output_capacity) {
            double best = INFINITY;
            size_t best_index = SIZE_MAX;

            for(i = 0; i < track->event_count; ++i) {
                double event_time = track->events[i].time;
                double cycle = floor((cursor - event_time) / period) + 1.0;
                double occurrence = cycle * period + event_time;

                if(occurrence <= cursor)
                    occurrence += period;
                if(occurrence <= shifted_current && occurrence < best) {
                    best = occurrence;
                    best_index = i;
                }
            }
            if(best_index == SIZE_MAX)
                break;
            output[published].event = track->events[best_index];
            output[published].direction = ANIM_PLAYBACK_FORWARD;
            ++published;
            cursor = best;
        }
    }

    collected.published_events = published;
    collected.truncated = collected.matching_events > published;
    if(result)
        *result = collected;
    return 0;
}

int pvr_cell_sprite_apply_transform(const pvr_cell_sprite_t *sprite,
                                    const anim_transform_t *transform,
                                    pvr_cell_sprite_t *output) {
    pvr_cell_sprite_t composed;
    double magnitude_squared;
    float x;
    float y;
    float z_angle;

    if(!sprite_valid(sprite) || !transform || !output ||
       !finite3(transform->translation.x, transform->translation.y,
                transform->translation.z) ||
       !finite3(transform->scale.x, transform->scale.y,
                transform->scale.z) ||
       transform->scale.x <= 0.0f || transform->scale.y <= 0.0f ||
       !isfinite(transform->rotation.w) ||
       !isfinite(transform->rotation.x) ||
       !isfinite(transform->rotation.y) ||
       !isfinite(transform->rotation.z)) {
        errno = EINVAL;
        return -1;
    }

    magnitude_squared =
        (double)transform->rotation.w * transform->rotation.w +
        (double)transform->rotation.x * transform->rotation.x +
        (double)transform->rotation.y * transform->rotation.y +
        (double)transform->rotation.z * transform->rotation.z;
    if(!isfinite(magnitude_squared) || magnitude_squared <= FLT_MIN) {
        errno = EINVAL;
        return -1;
    }
#ifdef __DREAMCAST__
    {
        shz_quat_t rotation = shz_quat_normalize(shz_quat_init(
            transform->rotation.w, transform->rotation.x,
            transform->rotation.y, transform->rotation.z));

        x = rotation.x;
        y = rotation.y;
        z_angle = shz_quat_angle_z(rotation);
    }
#else
    {
        float inverse_magnitude = (float)(1.0 / sqrt(magnitude_squared));
        float w;
        float z;

        w = transform->rotation.w * inverse_magnitude;
        x = transform->rotation.x * inverse_magnitude;
        y = transform->rotation.y * inverse_magnitude;
        z = transform->rotation.z * inverse_magnitude;
        z_angle = atan2f(2.0f * (w * z + x * y),
                         1.0f - 2.0f * (y * y + z * z));
    }
#endif
    if(fabsf(x) > 0.0001f || fabsf(y) > 0.0001f) {
        errno = ENOTSUP;
        return -1;
    }
    composed = *sprite;
    composed.position.x += transform->translation.x;
    composed.position.y += transform->translation.y;
    composed.position.z += transform->translation.z;
    composed.rotation += z_angle;
    composed.scale_x *= transform->scale.x;
    composed.scale_y *= transform->scale.y;
    if(!finite3(composed.position.x, composed.position.y,
                composed.position.z) || !isfinite(composed.rotation) ||
       !isfinite(composed.scale_x) || !isfinite(composed.scale_y) ||
       composed.scale_x <= 0.0f || composed.scale_y <= 0.0f) {
        errno = ERANGE;
        return -1;
    }
    *output = composed;
    return 0;
}

static uint32_t color_modulate(uint32_t lhs, uint32_t rhs) {
    uint32_t output = 0;
    unsigned shift;

    for(shift = 0; shift < 32u; shift += 8u) {
        uint32_t a = (lhs >> shift) & 0xffu;
        uint32_t b = (rhs >> shift) & 0xffu;
        uint32_t value = (a * b + 127u) / 255u;

        output |= value << shift;
    }
    return output;
}

static int resolve_cell(const pvr_cell_sprite_t *sprite,
                        const pvr_cell_state_t *state, size_t slot_index,
                        float sine, float cosine,
                        pvr_cell_resolved_t *output) {
    float local_x = state->offset.x * sprite->scale_x;
    float local_y = state->offset.y * sprite->scale_y;
    float rotated_x = local_x * cosine - local_y * sine;
    float rotated_y = local_x * sine + local_y * cosine;
    float scale_x = state->scale_x * sprite->scale_x;
    float scale_y = state->scale_y * sprite->scale_y;
    float rotation = state->rotation + sprite->rotation;
    size_t i;

    if(!isfinite(rotated_x) || !isfinite(rotated_y) ||
       !isfinite(scale_x) || !isfinite(scale_y) || scale_x <= 0.0f ||
       scale_y <= 0.0f || !isfinite(rotation) ||
       !finite3(sprite->position.x + rotated_x,
                sprite->position.y + rotated_y,
                sprite->position.z + state->offset.z)) {
        errno = ERANGE;
        return -1;
    }

    memset(output, 0, sizeof(*output));
    output->instance.cell_index = state->atlas_cell_index;
    output->instance.position.x = sprite->position.x + rotated_x;
    output->instance.position.y = sprite->position.y + rotated_y;
    output->instance.position.z = sprite->position.z + state->offset.z;
    output->instance.rotation = rotation;
    output->instance.scale_x = scale_x;
    output->instance.scale_y = scale_y;
    output->instance.flags = state->flags;
    output->slot_index = slot_index;
    output->priority = state->priority;
    output->material_id = state->material_id;
    for(i = 0; i < 4u; ++i) {
        output->argb[i] = color_modulate(state->argb[i], sprite->argb);
        output->oargb[i] = color_modulate(state->oargb[i], sprite->oargb);
    }
    return 0;
}

int pvr_cell_sprite_resolve(const pvr_cell_sprite_t *sprite,
                            const pvr_cell_state_t *cells,
                            size_t cell_count,
                            pvr_cell_resolved_t *output,
                            size_t output_capacity,
                            pvr_cell_resolve_result_t *result) {
    pvr_cell_resolve_result_t resolved = { 0, 0, 0 };
    float sine;
    float cosine;
    size_t input_bytes;
    size_t output_bytes;
    size_t i;

    if(result)
        *result = resolved;
    if(!sprite_valid(sprite) || !cells || cell_count != sprite->cell_count ||
       !output || output_capacity < cell_count ||
       ((uintptr_t)cells & (_Alignof(pvr_cell_state_t) - 1u)) ||
       ((uintptr_t)output & (_Alignof(pvr_cell_resolved_t) - 1u)) ||
       !range_valid((uintptr_t)cells, cell_count, sizeof(*cells)) ||
       !range_valid((uintptr_t)output, output_capacity, sizeof(*output))) {
        errno = EINVAL;
        return -1;
    }
    input_bytes = cell_count * sizeof(*cells);
    output_bytes = output_capacity * sizeof(*output);
    if(ranges_overlap((uintptr_t)cells, input_bytes,
                      (uintptr_t)output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(!state_valid(&cells[i])) {
            errno = EINVAL;
            return -1;
        }
    }

#ifdef __DREAMCAST__
    {
        shz_sincos_t value = shz_sincosf(sprite->rotation);

        sine = value.sin;
        cosine = value.cos;
    }
#else
    sine = sinf(sprite->rotation);
    cosine = cosf(sprite->rotation);
#endif
    if(!isfinite(sine) || !isfinite(cosine)) {
        errno = ERANGE;
        return -1;
    }

    /* Preflight every composed cell before publishing the first result. */
    for(i = 0; i < cell_count; ++i) {
        pvr_cell_resolved_t discarded;

        if(resolve_cell(sprite, &cells[i], i, sine, cosine,
                        &discarded) < 0)
            return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(resolve_cell(sprite, &cells[i], i, sine, cosine,
                        &output[i]) < 0)
            return -1;
        if(!(cells[i].flags & PVR_CELL_HIDDEN))
            ++resolved.visible_cells;
    }

    resolved.examined_cells = cell_count;
    resolved.resolved_cells = cell_count;
    if(result)
        *result = resolved;
    return 0;
}

static int resolved_cell_valid(const pvr_cell_resolved_t *cell) {
    return cell && finite3(cell->instance.position.x,
                           cell->instance.position.y,
                           cell->instance.position.z) &&
           isfinite(cell->instance.rotation) &&
           isfinite(cell->instance.scale_x) &&
           isfinite(cell->instance.scale_y) &&
           cell->instance.scale_x > 0.0f &&
           cell->instance.scale_y > 0.0f &&
           !(cell->instance.flags & ~PVR_CELL_FLAGS_ALL);
}

static int resolved_after(const pvr_cell_resolved_t *lhs,
                          const pvr_cell_resolved_t *rhs) {
    return lhs->priority > rhs->priority ||
           (lhs->priority == rhs->priority &&
            lhs->slot_index > rhs->slot_index);
}

int pvr_cell_resolved_sort(pvr_cell_resolved_t *cells, size_t cell_count) {
    size_t i;

    if((cell_count && !cells) ||
       (cells &&
        ((uintptr_t)cells & (_Alignof(pvr_cell_resolved_t) - 1u))) ||
       !range_valid((uintptr_t)cells, cell_count, sizeof(*cells))) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(!resolved_cell_valid(&cells[i])) {
            errno = EINVAL;
            return -1;
        }
    }

    /* Cell sprites are normally small. Stable insertion avoids a comparator
       and heap state while making equal-priority order deterministic. */
    for(i = 1; i < cell_count; ++i) {
        pvr_cell_resolved_t value = cells[i];
        size_t j = i;

        while(j && resolved_after(&cells[j - 1u], &value)) {
            cells[j] = cells[j - 1u];
            --j;
        }
        cells[j] = value;
    }
    return 0;
}

int pvr_cell_resolved_stream(const pvr_cell_resolved_t *cells,
                             size_t cell_count,
                             pvr_sprite_instance_stream_t *output) {
    size_t i;

    if(!output || (cell_count && !cells) ||
       (cells &&
        ((uintptr_t)cells & (_Alignof(pvr_cell_resolved_t) - 1u))) ||
       !range_valid((uintptr_t)cells, cell_count, sizeof(*cells))) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(!resolved_cell_valid(&cells[i])) {
            errno = EINVAL;
            return -1;
        }
    }

    output->instances = cells;
    output->instance_count = cell_count;
    output->stride = sizeof(*cells);
    return 0;
}

int pvr_cell_sprite_compile_2d(
        pvr_sprite_txr_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_cell_resolved_t *cells, size_t cell_count,
        pvr_sprite_batch_result_t *result) {
    pvr_sprite_instance_stream_t stream;

    if(pvr_cell_resolved_stream(cells, cell_count, &stream) < 0)
        return -1;
    return pvr_sprite_batch_compile_2d(output, output_capacity, atlas, &stream,
                                       result);
}

int pvr_cell_sprite_compile_3d(
        pvr_sprite_txr_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_cell_resolved_t *cells, size_t cell_count,
        const pvr_sprite_billboard_basis_t *basis,
        const matrix_t *world_to_screen,
        pvr_sprite_batch_result_t *result) {
    pvr_sprite_instance_stream_t stream;

    if(pvr_cell_resolved_stream(cells, cell_count, &stream) < 0)
        return -1;
    return pvr_sprite_batch_compile_3d(output, output_capacity, atlas, &stream,
                                       basis, world_to_screen, result);
}

static int atlas_cell_valid(const pvr_sprite_cell_t *cell) {
    return cell && isfinite(cell->width) && cell->width > 0.0f &&
           isfinite(cell->height) && cell->height > 0.0f &&
           isfinite(cell->origin_x) && isfinite(cell->origin_y) &&
           isfinite(cell->u0) && isfinite(cell->v0) &&
           isfinite(cell->u1) && isfinite(cell->v1) &&
           cell->u0 >= 0.0f && cell->v0 >= 0.0f &&
           cell->u1 <= 1.0f && cell->v1 <= 1.0f &&
           cell->u0 < cell->u1 && cell->v0 < cell->v1;
}

static int billboard_basis_valid(const pvr_sprite_billboard_basis_t *basis) {
    float x_length;
    float y_length;
    float cross_x;
    float cross_y;
    float cross_z;
    float cross_length;

    if(!basis || !finite3(basis->x_axis.x, basis->x_axis.y,
                          basis->x_axis.z) ||
       !finite3(basis->y_axis.x, basis->y_axis.y, basis->y_axis.z))
        return 0;
    x_length = basis->x_axis.x * basis->x_axis.x +
               basis->x_axis.y * basis->x_axis.y +
               basis->x_axis.z * basis->x_axis.z;
    y_length = basis->y_axis.x * basis->y_axis.x +
               basis->y_axis.y * basis->y_axis.y +
               basis->y_axis.z * basis->y_axis.z;
    cross_x = basis->x_axis.y * basis->y_axis.z -
              basis->x_axis.z * basis->y_axis.y;
    cross_y = basis->x_axis.z * basis->y_axis.x -
              basis->x_axis.x * basis->y_axis.z;
    cross_z = basis->x_axis.x * basis->y_axis.y -
              basis->x_axis.y * basis->y_axis.x;
    cross_length = cross_x * cross_x + cross_y * cross_y +
                   cross_z * cross_z;
    return isfinite(x_length) && x_length > FLT_MIN &&
           isfinite(y_length) && y_length > FLT_MIN &&
           isfinite(cross_length) && cross_length > FLT_MIN;
}

static int transform_valid(const matrix_t *matrix) {
    size_t column;
    size_t row;

    if(!matrix || ((uintptr_t)matrix & (_Alignof(matrix_t) - 1u)))
        return 0;
    for(column = 0; column < 4u; ++column) {
        for(row = 0; row < 4u; ++row) {
            if(!isfinite((*matrix)[column][row]))
                return 0;
        }
    }
    return 1;
}

static int colored_preflight(pvr_vertex_t *output, size_t output_capacity,
                             const pvr_sprite_atlas_t *atlas,
                             const pvr_cell_resolved_t *cells,
                             size_t cell_count, size_t *visible_count) {
    size_t atlas_bytes;
    size_t input_bytes;
    size_t output_bytes;
    size_t visible = 0;
    size_t i;

    if(!output || ((uintptr_t)output & 31u) || !atlas || !atlas->cells ||
       !atlas->cell_count ||
       ((uintptr_t)atlas->cells & (_Alignof(pvr_sprite_cell_t) - 1u)) ||
       (cell_count && !cells) ||
       (cells &&
        ((uintptr_t)cells & (_Alignof(pvr_cell_resolved_t) - 1u))) ||
       !range_valid((uintptr_t)atlas->cells, atlas->cell_count,
                    sizeof(*atlas->cells)) ||
       !range_valid((uintptr_t)cells, cell_count, sizeof(*cells)) ||
       !range_valid((uintptr_t)output, output_capacity, sizeof(*output))) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < atlas->cell_count; ++i) {
        if(!atlas_cell_valid(&atlas->cells[i])) {
            errno = EINVAL;
            return -1;
        }
    }
    for(i = 0; i < cell_count; ++i) {
        if(!resolved_cell_valid(&cells[i]) ||
           cells[i].instance.cell_index >= atlas->cell_count) {
            errno = EINVAL;
            return -1;
        }
        if(!(cells[i].instance.flags & PVR_CELL_HIDDEN))
            ++visible;
    }
    if(visible > SIZE_MAX / 4u || output_capacity < visible * 4u) {
        errno = visible > SIZE_MAX / 4u ? ERANGE : ENOSPC;
        return -1;
    }

    atlas_bytes = atlas->cell_count * sizeof(*atlas->cells);
    input_bytes = cell_count * sizeof(*cells);
    output_bytes = output_capacity * sizeof(*output);
    if(ranges_overlap((uintptr_t)output, output_bytes,
                      (uintptr_t)atlas->cells, atlas_bytes) ||
       ranges_overlap((uintptr_t)output, output_bytes,
                      (uintptr_t)cells, input_bytes)) {
        errno = EINVAL;
        return -1;
    }
    *visible_count = visible;
    return 0;
}

static int build_colored_quad(const pvr_sprite_cell_t *cell,
                              const pvr_cell_resolved_t *resolved,
                              const pvr_sprite_billboard_basis_t *basis,
                              pvr_vertex_t output[4]) {
    const pvr_sprite_instance_t *instance = &resolved->instance;
    float left = -cell->origin_x * cell->width * instance->scale_x;
    float right = (1.0f - cell->origin_x) * cell->width * instance->scale_x;
    float top = -cell->origin_y * cell->height * instance->scale_y;
    float bottom = (1.0f - cell->origin_y) * cell->height * instance->scale_y;
    float local_x[4] = { left, left, right, right };
    float local_y[4] = { bottom, top, top, bottom };
    float u_left = cell->u0;
    float u_right = cell->u1;
    float v_top = cell->v0;
    float v_bottom = cell->v1;
    float sine;
    float cosine;
    size_t i;

#ifdef __DREAMCAST__
    {
        shz_sincos_t value = shz_sincosf(instance->rotation);

        sine = value.sin;
        cosine = value.cos;
    }
#else
    sine = sinf(instance->rotation);
    cosine = cosf(instance->rotation);
#endif
    if(!isfinite(sine) || !isfinite(cosine)) {
        errno = ERANGE;
        return -1;
    }

    if(instance->flags & PVR_CELL_FLIP_U) {
        float swap = u_left;

        u_left = u_right;
        u_right = swap;
    }
    if(instance->flags & PVR_CELL_FLIP_V) {
        float swap = v_top;

        v_top = v_bottom;
        v_bottom = swap;
    }

    memset(output, 0, 4u * sizeof(*output));
    for(i = 0; i < 4u; ++i) {
        float rotated_x = local_x[i] * cosine - local_y[i] * sine;
        float rotated_y = local_x[i] * sine + local_y[i] * cosine;

        output[i].x = instance->position.x +
                      basis->x_axis.x * rotated_x +
                      basis->y_axis.x * rotated_y;
        output[i].y = instance->position.y +
                      basis->x_axis.y * rotated_x +
                      basis->y_axis.y * rotated_y;
        output[i].z = instance->position.z +
                      basis->x_axis.z * rotated_x +
                      basis->y_axis.z * rotated_y;
        output[i].flags = i == 3u ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        output[i].argb = resolved->argb[i];
        output[i].oargb = resolved->oargb[i];
        if(!finite3(output[i].x, output[i].y, output[i].z)) {
            errno = ERANGE;
            return -1;
        }
    }
    output[0].u = u_left;
    output[0].v = v_bottom;
    output[1].u = u_left;
    output[1].v = v_top;
    output[2].u = u_right;
    output[2].v = v_top;
    output[3].u = u_right;
    output[3].v = v_bottom;
    return 0;
}

static int compile_colored_world(
        pvr_vertex_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_cell_resolved_t *cells, size_t cell_count,
        const pvr_sprite_billboard_basis_t *basis,
        pvr_sprite_batch_result_t *result) {
    pvr_sprite_batch_result_t compiled = { 0, 0 };
    size_t visible_count;
    size_t produced = 0;
    size_t i;

    if(result)
        *result = compiled;
    if(colored_preflight(output, output_capacity, atlas, cells, cell_count,
                         &visible_count) < 0 ||
       !billboard_basis_valid(basis)) {
        if(errno == 0)
            errno = EINVAL;
        return -1;
    }

    for(i = 0; i < cell_count; ++i) {
        pvr_vertex_t discarded[4];

        if(!(cells[i].instance.flags & PVR_CELL_HIDDEN) &&
           build_colored_quad(&atlas->cells[cells[i].instance.cell_index],
                              &cells[i], basis, discarded) < 0)
            return -1;
    }
    for(i = 0; i < cell_count; ++i) {
        if(cells[i].instance.flags & PVR_CELL_HIDDEN)
            continue;
        if(build_colored_quad(&atlas->cells[cells[i].instance.cell_index],
                              &cells[i], basis, &output[produced * 4u]) < 0)
            return -1;
        ++produced;
    }

    compiled.examined_instances = cell_count;
    compiled.produced_sprites = visible_count;
    if(result)
        *result = compiled;
    return 0;
}

int pvr_cell_sprite_compile_colored_2d(
        pvr_vertex_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_cell_resolved_t *cells, size_t cell_count,
        pvr_sprite_batch_result_t *result) {
    const pvr_sprite_billboard_basis_t basis = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f }
    };

    return compile_colored_world(output, output_capacity, atlas, cells,
                                 cell_count, &basis, result);
}

int pvr_cell_sprite_compile_colored_3d(
        pvr_vertex_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_cell_resolved_t *cells, size_t cell_count,
        const pvr_sprite_billboard_basis_t *basis,
        const matrix_t *world_to_screen,
        pvr_sprite_batch_result_t *result) {
    pvr_sprite_batch_result_t compiled = { 0, 0 };
    pvr_geometry_stream_t stream;
    pvr_geometry_result_t projected;
    size_t vertex_count;

    if(result)
        *result = compiled;
    if(!transform_valid(world_to_screen)) {
        errno = EINVAL;
        return -1;
    }
    if(compile_colored_world(output, output_capacity, atlas, cells, cell_count,
                             basis, &compiled) < 0)
        return -1;

    vertex_count = compiled.produced_sprites * 4u;
    stream.vertices = output;
    stream.vertex_count = vertex_count;
    stream.stride = sizeof(*output);
    if(pvr_geometry_project(output, output_capacity, &stream,
                            world_to_screen, &projected) < 0) {
        compiled.produced_sprites = projected.produced_vertices / 4u;
        if(result)
            *result = compiled;
        return -1;
    }
    if(result)
        *result = compiled;
    return 0;
}
