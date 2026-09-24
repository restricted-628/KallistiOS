/* KallistiOS ##version##
   Independent authored-grid TRS/normal/light oracle, shared by host and target.
   Copyright (C) 2026 Joseph Black
*/

/* Deliberately use scalar double arithmetic and libm, not animation, matrix,
   skinning, normal-matrix, or SH4ZAM helpers used by the implementation. */
typedef struct grid_oracle {
    double bend, sine, cosine, sx, sy, sz;
} grid_oracle_t;

typedef struct grid_expected {
    double x, y, z, nx, ny, nz;
} grid_expected_t;

static grid_oracle_t grid_oracle(float time) {
    const double bend = time <= 1.0f ? time : 2.0 - time;
    const double angle = bend * 1.57079632679489661923;
    const grid_oracle_t result = {
        bend, sin(angle), cos(angle), 1.0 + bend, 1.0 + 0.5 * bend, 1.0 - 0.5 * bend
    };
    return result;
}

static grid_expected_t grid_expected(const grid_oracle_t *o, size_t vertex,
                                     double morph) {
    const double influence = (double)(vertex / 17u) / 16.0;
    const double source_x = (double)(vertex % 17u) / 8.0 - 1.0;
    const double x = source_x + (vertex + 1u == VERTICES ? 0.5 * morph : 0.0);
    const double y = 2.0 * influence - 1.0;
    const double z = source_x * 0.5;
    const double nx = -1.0 / sqrt(5.0), nz = 2.0 / sqrt(5.0);
    /* The inverse bind subtracts (0,1,0) BEFORE tip T*R*S. Root X=.25
       is applied once, after blending the two world-space joint results. */
    grid_expected_t result = {
        0.25 + (1.0 - influence) * x + influence *
            (o->bend + o->cosine * o->sx * x + o->sine * o->sz * z),
        (1.0 - influence) * y + influence * (1.0 + o->sy * (y - 1.0)),
        (1.0 - influence) * z + influence *
            (-o->sine * o->sx * x + o->cosine * o->sz * z),
        (1.0 - influence) * nx + influence *
            (o->cosine * nx / o->sx + o->sine * nz / o->sz),
        0.0,
        (1.0 - influence) * nz + influence *
            (-o->sine * nx / o->sx + o->cosine * nz / o->sz)
    };
    /* Contract: blend inverse-transpose normals, THEN normalize. This is
       not a geometric-normal reconstruction from the deformed triangles. */
    const double length = sqrt(result.nx * result.nx + result.nz * result.nz);
    result.nx /= length;
    result.nz /= length;
    return result;
}

static int grid_check_vertex(const pvr_deform_vertex_t *v,
                              const grid_expected_t *e) {
    const double tolerance = 0.0005;
    return require(fabs(v->position.x - e->x) < tolerance &&
                   fabs(v->position.y - e->y) < tolerance &&
                   fabs(v->position.z - e->z) < tolerance &&
                   fabs(v->normal.x - e->nx) < tolerance &&
                   fabs(v->normal.y - e->ny) < tolerance &&
                   fabs(v->normal.z - e->nz) < tolerance &&
                   fabs((double)v->normal.x * v->normal.x +
                        (double)v->normal.y * v->normal.y +
                        (double)v->normal.z * v->normal.z - 1.0) < tolerance);
}

static int grid_shade(const pvr_chunk_render_state_t *state,
                       uint16_t index, const pvr_deform_vertex_t *vertex,
                       pvr_vertex_t *output, void *data) {
    static const pvr_light_t light = {
        .kind = PVR_LIGHT_DIRECTIONAL,
        .source.direction = { .z = 1 },
        .color = { .x = 1, .y = 1, .z = 1 },
        .intensity = 0.65f
    };
    static const pvr_lighting_context_t lighting = { {0.2f, 0.2f, 0.2f}, &light, 1 };
    const uint32_t color = output->argb;
    const pvr_lighting_sample_t input = {
        vertex->position, vertex->normal,
        { ((color >> 16) & 255u) / 255.0f,
          ((color >> 8) & 255u) / 255.0f,
          (color & 255u) / 255.0f, (color >> 24) / 255.0f }
    };
    const pvr_lighting_stream_t stream = { &input, 1, sizeof(input) };
    pvr_lighting_result_t result;
    (void)state;
    (void)index;
    (void)data;
    if(pvr_lighting_apply(&output->argb, 1, &stream, &lighting, &result) < 0)
        return -1;
    return require(result.shaded_samples == 1);
}

static int grid_check_color(uint32_t actual, uint32_t base,
                             const grid_expected_t *e) {
    const double diffuse = e->nz > 0.0 ? e->nz : 0.0;
    const double scale = 0.2 + 0.65 * diffuse;
    if(require((actual >> 24) == (base >> 24)) < 0)
        return -1;
    for(unsigned shift = 0; shift < 24; shift += 8) {
        const int expected = (int)floor(((base >> shift) & 255u) * scale + 0.5);
        const int channel = (actual >> shift) & 255u;
        /* One code point permits different rounding at a quantization edge;
           normal/position goldens above retain their independent tolerance. */
        if(require(abs(channel - expected) <= 1) < 0)
            return -1;
    }
    return 0;
}
