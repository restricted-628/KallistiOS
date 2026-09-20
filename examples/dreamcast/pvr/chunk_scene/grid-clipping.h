/* KallistiOS ##version##
   Animated Compact-grid clipping conformance, shared by host and target.
   Copyright (C) 2026 Joseph Black
*/

#define GRID_CLIP_CASES 6u
#define GRID_CLIP_CAPACITY (TRIANGLES * PVR_FRUSTUM_CLIP_MAX_VERTICES)

typedef struct grid_clip_context {
    model_state_t *model;
    size_t prepared;
} grid_clip_context_t;

/* A private snapshot borrows immutable streams/index storage, but supplies a
   current-pose bound. Never mutate the admitted plan or serialized asset. */
static int grid_clip_plan(model_state_t *m, pvr_chunk_model_plan_t *plan) {
    const pvr_deform_stream_t stream = {
        m->deformed, VERTICES, sizeof(*m->deformed)
    };
    pvr_deform_bounds_t bounds;
    if(pvr_deform_bounds_calculate(&stream, &bounds) < 0)
        return -1;
    *plan = m->plan;
    plan->view.model.center[0] = bounds.center.x;
    plan->view.model.center[1] = bounds.center.y;
    plan->view.model.center[2] = bounds.center.z;
    plan->view.model.radius = bounds.radius;
    return 0;
}

static uint32_t grid_offset_color(size_t index) {
    const unsigned row = index / 17u, column = index % 17u;
    return ((64u + row * 8u) << 24) | ((column * 12u) << 16) |
           ((row * 12u) << 8) | ((row + column) * 6u);
}

static int grid_clip_vertex(const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes,
    const pvr_chunk_strip_attributes_t *strip, pvr_vertex_t *vertex, void *data) {
    grid_clip_context_t *context = data;
    pvr_deform_vertex_t posed;
    if(require(attributes->index == strip->index) < 0 ||
       resolve(attributes->index, &posed, context->model) < 0)
        return -1;
    ++context->prepared;
    vertex->x = posed.position.x;
    vertex->y = posed.position.y;
    vertex->z = posed.position.z;
    vertex->oargb = grid_offset_color(attributes->index);
    return grid_shade(state, attributes->index, &posed, vertex, context->model);
}

static int grid_clip_frustum(unsigned which, size_t model, pvr_frustum_t *frustum,
                             pvr_chunk_clip_policy_t *policy) {
    alignas(32) matrix_t matrix = {
        { 110, 0, 0, 0 }, { 0, 95, 0, 0 },
        { 0, 0, 1, 0 }, { 320, 240, 0, 1 }
    };
    float left = -2000, right = 2000, top = -2000, bottom = 2000;
    float near = 0.1f, far = 4;
    *policy = which == 2 || which == 4 ? PVR_CHUNK_CLIP_DROP : PVR_CHUNK_CLIP_SPLIT;
    if(which == 1 || which == 2) {
        left = 260.3f; right = 420.7f; top = 190.2f; bottom = 300.6f;
    }
    if(which == 3 || which == 4) {
        /* W=1+.35X+.25Z; Xh=320W+110X, Yh=240W+95Y.
           Depth is homogeneous W, not transformed Z. */
        matrix[0][0] += 320 * 0.35f;
        matrix[0][1] = 240 * 0.35f;
        matrix[0][3] = 0.35f;
        matrix[2][0] = 320 * 0.25f;
        matrix[2][1] = 240 * 0.25f;
        matrix[2][3] = 0.25f;
        left = 0; right = 640; top = 0; bottom = 480;
        near = 0.87f; far = 1.23f;
    }
    if(which == 5)
        matrix[3][0] = 5000;
    /* Two adjacent display panes, applied after world-space deformation. */
    const float offset = model ? 320 : 0;
    for(size_t row = 0; row < 4; ++row)
        matrix[row][0] = 0.5f*matrix[row][0] + offset*matrix[row][3];
    left = 0.5f*left + offset;
    right = 0.5f*right + offset;
    return pvr_frustum_init(frustum, &matrix, left, top, right, bottom, near, far);
}

/* Independent double-precision oracle: authored triangles, scalar projection,
   convex-polygon clipping in reverse plane order, then projected shoelace area.
   No SDK classify/clip/project or cache topology is used here. */
typedef struct grid_clip_point { double x, y, w; } grid_clip_point_t;

static grid_clip_point_t grid_clip_project(const pvr_frustum_t *f,
                                          const grid_expected_t *v) {
    const float (*m)[4] = f->object_to_screen;
    const grid_clip_point_t result = {
        m[0][0]*v->x + m[1][0]*v->y + m[2][0]*v->z + m[3][0],
        m[0][1]*v->x + m[1][1]*v->y + m[2][1]*v->z + m[3][1],
        m[0][3]*v->x + m[1][3]*v->y + m[2][3]*v->z + m[3][3]
    };
    return result;
}

static double grid_clip_distance(const pvr_frustum_t *f,
                                 grid_clip_point_t p, unsigned plane) {
    switch(plane) {
        case 0: return p.x - f->left*p.w;
        case 1: return f->right*p.w - p.x;
        case 2: return p.y - f->top*p.w;
        case 3: return f->bottom*p.w - p.y;
        case 4: return p.w - f->w_near;
        default: return f->w_far - p.w;
    }
}

static double grid_clip_area(const pvr_frustum_t *f,
    pvr_chunk_clip_policy_t policy, const grid_expected_t expected[VERTICES],
    size_t *whole_triangles, unsigned *crossed) {
    double area = 0;
    *whole_triangles = 0;
    *crossed = 0;
    for(size_t row = 0; row < 16; ++row) {
        for(size_t column = 0; column < 16; ++column) {
            const size_t bl = row*17 + column, tr = bl + 18;
            const size_t indices[2][3] = { {bl, tr, bl+17}, {bl, bl+1, tr} };
            for(size_t triangle = 0; triangle < 2; ++triangle) {
                grid_clip_point_t polygon[12], next[12];
                size_t count = 3;
                int whole = 1;
                for(size_t v = 0; v < 3; ++v)
                    polygon[v] = grid_clip_project(f, &expected[indices[triangle][v]]);
                for(unsigned plane = 0; plane < 6; ++plane) {
                    unsigned inside = 0;
                    for(size_t v = 0; v < 3; ++v)
                        inside += grid_clip_distance(f, polygon[v], plane) >= 0;
                    if(inside != 3)
                        whole = 0;
                    if(inside > 0 && inside < 3)
                        *crossed |= 1u << plane;
                }
                *whole_triangles += whole;
                if(policy == PVR_CHUNK_CLIP_DROP && !whole)
                    continue;
                for(int plane = 5; plane >= 0 && count; --plane) {
                    size_t used = 0;
                    for(size_t v = 0; v < count; ++v) {
                        const grid_clip_point_t a = polygon[v], b = polygon[(v+1)%count];
                        const double da = grid_clip_distance(f, a, (unsigned)plane);
                        const double db = grid_clip_distance(f, b, (unsigned)plane);
                        if(da >= 0)
                            next[used++] = a;
                        if((da < 0) != (db < 0)) {
                            const double t = da / (da-db);
                            next[used++] = (grid_clip_point_t){
                                a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), a.w + t*(b.w-a.w)
                            };
                        }
                    }
                    count = used;
                    memcpy(polygon, next, count*sizeof(*polygon));
                }
                double signed_area = 0;
                for(size_t v = 0; v < count; ++v) {
                    const grid_clip_point_t a = polygon[v], b = polygon[(v+1)%count];
                    signed_area += (a.x*b.y - b.x*a.y) / (a.w*b.w);
                }
                area += fabs(signed_area)*0.5;
            }
        }
    }
    return area;
}

/* UVs locate a point in the ORIGINAL authored triangle. Reconstruct position
   and quantized endpoint colors barycentrically, independently of clip order. */
static int grid_clip_packet(const pvr_vertex_t *v, const pvr_frustum_t *f,
    const grid_expected_t expected[VERTICES], uint32_t base) {
    if(require(isfinite(v->u) && isfinite(v->v) && v->u >= -0.00001f &&
               v->u <= 1.00001f && v->v >= -0.00001f && v->v <= 1.00001f) < 0)
        return -1;
    const double u = fmin(16.0, fmax(0.0, (double)v->u*16));
    const double w = fmin(16.0, fmax(0.0, (double)v->v*16));
    const size_t column = (size_t)fmin(15, floor(u)), row = (size_t)fmin(15, floor(w));
    const double x = u-column, y = w-row;
    const size_t bl = row*17 + column;
    const size_t indices[3] = { bl, y >= x ? bl+17 : bl+1, bl+18 };
    const double weights[3] = { 1-fmax(x,y), fabs(y-x), fmin(x,y) };
    grid_expected_t p = {0};
    double argb[4] = {0}, oargb[4] = {0};
    for(size_t j = 0; j < 3; ++j) {
        const grid_expected_t *e = &expected[indices[j]];
        const double scale = 0.2 + 0.65*fmax(0, e->nz);
        const uint32_t offset = grid_offset_color(indices[j]);
        p.x += weights[j]*e->x; p.y += weights[j]*e->y; p.z += weights[j]*e->z;
        for(unsigned channel = 0; channel < 4; ++channel) {
            const unsigned shift = channel*8;
            const double color = (base >> shift) & 255u;
            argb[channel] += weights[j]*(channel == 3 ? color : floor(color*scale + 0.5));
            oargb[channel] += weights[j]*((offset >> shift) & 255u);
        }
    }
    const grid_clip_point_t projected = grid_clip_project(f, &p);
    if(require(isfinite(v->x) && isfinite(v->y) && isfinite(v->z) &&
               fabs(v->x - projected.x/projected.w) < 0.02 &&
               fabs(v->y - projected.y/projected.w) < 0.02 &&
               fabs(v->z - 1/projected.w) < 0.0002 &&
               v->x >= f->left-0.02 && v->x <= f->right+0.02 &&
               v->y >= f->top-0.02 && v->y <= f->bottom+0.02 &&
               1/v->z >= f->w_near-0.0002 && 1/v->z <= f->w_far+0.0002) < 0)
        return -1;
    for(unsigned channel = 0; channel < 4; ++channel) {
        const unsigned shift = channel*8;
        /* Up to six clipping planes quantize packed colors at intersections. */
        if(require(fabs(((v->argb >> shift)&255u)-argb[channel]) <= 4 &&
                   fabs(((v->oargb >> shift)&255u)-oargb[channel]) <= 4) < 0)
            return -1;
    }
    return require((v->argb >> 24) == (base >> 24));
}

static int grid_clip_emit(const pvr_chunk_model_plan_t *plan,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t policy,
    pvr_geometry_sink_t *sink, int prepared, grid_clip_context_t *context,
    pvr_chunk_render_begin_strip_t begin, pvr_chunk_render_result_t *result) {
    alignas(32) pvr_vertex_t workspace[STRIP_VERTICES];
    alignas(32) pvr_vertex_t clips[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    context->prepared = 0;
    if(prepared)
        return pvr_chunk_model_emit_clipped_prepared(plan, frustum, policy,
            sink, workspace, STRIP_VERTICES, clips, PVR_FRUSTUM_CLIP_MAX_VERTICES,
            begin, grid_clip_vertex, context, result);
    return pvr_chunk_model_emit_clipped(&plan->view, frustum, policy,
        sink, workspace, STRIP_VERTICES, clips, PVR_FRUSTUM_CLIP_MAX_VERTICES,
        begin, grid_clip_vertex, context, result);
}

static int grid_clip_check(void) {
    static const float times[] = {0, 0.25f, 0.5f, 1, 1.5f, 2};
    pvr_vertex_t *output = aligned_alloc(32, (GRID_CLIP_CAPACITY+1)*sizeof(*output));
    pvr_vertex_t *raw = aligned_alloc(32, (GRID_CLIP_CAPACITY+1)*sizeof(*raw));
    const pvr_vertex_t guard = { .flags = 0xdeadbeef };
    unsigned crossed_planes = 0;
    size_t checks = 0;
    size_t test_time = 0, test_model = 0;
    unsigned test_case = 0;
    const char *stage = "allocation";
    int rv = -1;
    if(!output || !raw)
        goto out;
    for(size_t t = 0; t < sizeof(times)/sizeof(*times); ++t) {
        test_time = t;
        stage = "sample";
        const grid_oracle_t oracle = grid_oracle(times[t]);
        if(sample(times[t]) < 0)
            goto out;
        for(size_t model = 0; model < MODELS; ++model) {
            test_model = model;
            stage = "bounds";
            model_state_t *m = &app.model[model];
            grid_clip_context_t context = { m, 0 };
            pvr_chunk_model_plan_t plan;
            grid_expected_t expected[VERTICES];
            const double morph = model ? 1-oracle.bend : oracle.bend;
            if(grid_clip_plan(m, &plan) < 0)
                goto out;
            for(size_t i = 0; i < VERTICES; ++i) {
                expected[i] = grid_expected(&oracle, i, morph);
                const double dx = expected[i].x-plan.view.model.center[0];
                const double dy = expected[i].y-plan.view.model.center[1];
                const double dz = expected[i].z-plan.view.model.center[2];
                if(require(sqrt(dx*dx+dy*dy+dz*dz) <= plan.view.model.radius+0.0005) < 0)
                    goto out;
            }
            for(unsigned which = 0; which < GRID_CLIP_CASES; ++which) {
                test_case = which;
                stage = "classification";
                pvr_frustum_t frustum;
                pvr_chunk_clip_policy_t policy;
                pvr_frustum_classification_t classification;
                pvr_geometry_sink_t sink;
                pvr_chunk_render_result_t result, reference;
                size_t whole;
                unsigned crossed;
                if(grid_clip_frustum(which, model, &frustum, &policy) < 0 ||
                   pvr_chunk_model_classify(&plan.view, &frustum, &classification) < 0 ||
                   require(classification == (which == 0 ? PVR_FRUSTUM_INSIDE :
                       which == 5 ? PVR_FRUSTUM_OUTSIDE : PVR_FRUSTUM_INTERSECT)) < 0)
                    goto out;
                const double area = grid_clip_area(&frustum, policy, expected, &whole, &crossed);
                crossed_planes |= crossed;
                for(size_t i = 0; i <= GRID_CLIP_CAPACITY; ++i)
                    output[i] = raw[i] = guard;
                stage = "emission-parity";
                if(pvr_geometry_sink_init_memory(&sink, output, GRID_CLIP_CAPACITY) < 0 ||
                   grid_clip_emit(&plan, &frustum, policy, &sink, 1, &context, NULL, &result) < 0 ||
                   require(context.prepared == (which == 5 ? 0 : PACKETS)) < 0 ||
                   require(result.emitted_vertices <= GRID_CLIP_CAPACITY &&
                           sink.emitted_vertices == result.emitted_vertices) < 0 ||
                   pvr_geometry_sink_init_memory(&sink, raw, GRID_CLIP_CAPACITY) < 0 ||
                   grid_clip_emit(&plan, &frustum, policy, &sink, 0, &context, NULL, &reference) < 0 ||
                   require(result.emitted_vertices == reference.emitted_vertices &&
                           result.emitted_strips == reference.emitted_strips &&
                           memcmp(output, raw, (GRID_CLIP_CAPACITY+1)*sizeof(*raw)) == 0) < 0)
                    goto out;
                const size_t count = result.emitted_vertices;
                stage = "counts-guards";
                if(require(memcmp(output+count, &guard, sizeof(guard)) == 0 &&
                           memcmp(output+GRID_CLIP_CAPACITY, &guard, sizeof(guard)) == 0 &&
                           (which != 0 || count == PACKETS) &&
                           (which != 5 || (count == 0 && area == 0)) &&
                           (policy != PVR_CHUNK_CLIP_DROP || count == whole*3)) < 0)
                    goto out;
                double actual_area = 0;
                const size_t strip_size = which == 0 ? STRIP_VERTICES : 3;
                stage = "packet";
                for(size_t i = 0; i < count; ++i) {
                    if(grid_clip_packet(output+i, &frustum, expected, m->cache.vertices[0].argb) < 0 ||
                       require(output[i].flags == ((i+1)%strip_size ? PVR_CMD_VERTEX : PVR_CMD_VERTEX_EOL)) < 0)
                        goto out;
                    if(i%strip_size >= 2) {
                        const pvr_vertex_t *a = output+i-2, *b = output+i-1, *c = output+i;
                        actual_area += fabs(((double)b->x-a->x)*(c->y-a->y) -
                                            ((double)b->y-a->y)*(c->x-a->x))*0.5;
                    }
                }
                stage = "area";
                if(require(fabs(actual_area-area) < 0.5 + area*0.00005) < 0) {
                    printf("clip area actual=%g expected=%g\n", actual_area, area);
                    goto out;
                }
                stage = "short-sink";
                if(count > 1) {
                    const size_t capacity = count-1;
                    output[capacity] = guard;
                    if(pvr_geometry_sink_init_memory(&sink, output, capacity) < 0)
                        goto out;
                    errno = 0;
                    if(require(grid_clip_emit(&plan, &frustum, policy, &sink, 1, &context,
                                              NULL, &reference) == -1 && errno == ENOSPC &&
                               reference.emitted_vertices == sink.emitted_vertices &&
                               sink.emitted_vertices <= capacity &&
                               memcmp(output+capacity, &guard, sizeof(guard)) == 0) < 0)
                        goto out;
                }
                ++checks;
            }
        }
    }
    stage = "coverage";
    if(require(checks == 72 && crossed_planes == 63) < 0)
        goto out;
    puts("KOSSCENE clip_checks=72 planes=6 pose_bounds=PASS area_uv_color=PASS guards=PASS");
    rv = 0;
out:
    {
        const int error = errno;
        if(rv < 0)
            printf("clip failure time=%g model=%u case=%u check=%s\n",
                   (double)times[test_time], (unsigned)test_model, test_case, stage);
        free(raw);
        free(output);
        errno = error;
    }
    return rv < 0 ? failure("grid-clipping") : 0;
}
