/* KallistiOS ##version## */
#ifndef KOS_SH4DER_WORLD_H
#define KOS_SH4DER_WORLD_H
#include <dc/pvr_chunk_render.h>
#include <dc/sh4zam.h>
#include <sh4der_kos_trail.h>
#include <sh4der/sh4der_pool.h>
#include <sh4der/sh4der_spark.h>
#include <sh4der/sh4der_pl4nar.h>
#include <sh4der/sh4der_water.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Convert the returned disc/wedge fan into ordinary world-space KOS triangle
   strips. count is the upstream vertex count, not the number of triangles.
   Closed for discs, open for wedges. Project/clip with the scene camera. */
int sh4der_kos_pool_vertices(const sh4der_pool_vtx_t *fan, unsigned count, bool closed,
                             pvr_vertex_t *out, size_t capacity);

/* One live particle -> four screen-space packets, or zero if clipped or
   degenerate. Finite camera/particle data and positive pixel focal lengths.
   Do not reproject. Caller owns the atlas and additive header. */
int sh4der_kos_spark_vertices(const sh4der_spark_t *spark, const sh4der_spark_view_t *view,
                              const sh4der_spark_screen_t *screen, pvr_vertex_t out[4]);

/* A Y-axis billboard model transform for Compact. Finite pos/eye, distinct
   in the XZ plane. Does not leave the billboard in the caller's XMTRX. */
void sh4der_kos_billboard_y(matrix_t *out, shz_vec3_t pos, shz_vec3_t eye);

/* Build a mirrored camera and its KOS world->screen projection together.
   Unit plane normal, positive near distance, all vectors in world space.
   Reverse culling for the mirrored draw. Preserve XMTRX across preparation. */
int sh4der_kos_mirror_view(sh4der_pl4nar_view_t *out, matrix_t *projection,
                           const sh4der_pl4nar_plane_t *plane, const float eye[3],
                           const float target[3], const float up[3], float fov, float near_z,
                           unsigned w, unsigned h, float fade);

/* Bind a published retained RGB565 image to a depth-tested TR world layer.
   Use trail_bind/invalidate/publish for exact capture identity. dst is NULL
   for display, otherwise the nonoverlapping destination's texture address.
   Bind the image UVs for the captured content dimensions (not padded size).
   Keep source alive through reader retirement. Honor shared stride state.
   Reuse the resulting header for all matching surfaces in this pass. */
int sh4der_kos_mirror_header(pvr_poly_hdr_t *out, const sh4der_kos_trail_history_t *capture,
                             sh4der_kos_frame_id_t expected, pvr_ptr_t dst, bool additive);

/* Prepare flat (normals NULL) or curved reflection samples at pose/camera
   update, NOT in the strip loop. pos/normals are dense float[count][3],
   world space and normals unit. XMTRX restored once per batch. */
int sh4der_kos_mirror_samples(const sh4der_pl4nar_view_t *view,
                              const sh4der_pl4nar_material_t *material, const float (*pos)[3],
                              const float (*normals)[3], sh4der_pl4nar_sample_t *out,
                              unsigned count);

/* Samples indexed by model vertex including holes. Caller must supply enough
   entries for the admitted maximum index. Bind same image used by header.
   Immutable until draw returns; refresh on pose, camera or image replacement. */
typedef struct sh4der_kos_mirror_draw {
    const sh4der_pl4nar_sample_t *samples;
    const sh4der_pl4nar_material_t *material;
    const sh4der_kos_image_t *image;
} sh4der_kos_mirror_draw_t;
int sh4der_kos_mirror_vertex(const pvr_chunk_render_state_t *,
                             const pvr_chunk_vertex_attributes_t *,
                             const pvr_chunk_strip_attributes_t *, pvr_vertex_t *, void *);

/* Prepared once per vertex at pose/update time. Additive transmission is in
   linear normalized RGB; alpha is never modified. The same sidecar handles
   solid SSS (dy=0) and displaced water. Like mirror samples, index by the
   admitted model vertex, including holes. Destination is pre-projection. */
typedef struct sh4der_kos_surface_sample {
    float dy;
    float transmission[3];
} sh4der_kos_surface_sample_t;

/* Water is a Y-up height field in model/world XZ. Sample per unique model
   vertex at update time, never per repeated strip reference. Finite inputs;
   unit view/light point FROM the displaced surface toward the eye/light.
   crest and visibility are 0..1, paths nonnegative; footprint is in world
   units. normal receives the matching unit normal for other lighting passes. */
typedef struct sh4der_kos_water_material {
    const sh4der_water_wave_t *waves;
    unsigned count;
    float swell, ripple, thin_path, thick_path;
    sh4der_sss_t scattering;
} sh4der_kos_water_material_t;
void sh4der_kos_water_sample(const sh4der_kos_water_material_t *material, float x, float z,
                             float time, float footprint, const float view[3], const float light[3],
                             float crest, float visibility, sh4der_kos_surface_sample_t *out,
                             float normal[3]);
int sh4der_kos_surface_vertex(const pvr_chunk_render_state_t *,
                              const pvr_chunk_vertex_attributes_t *,
                              const pvr_chunk_strip_attributes_t *, pvr_vertex_t *, void *samples);

/* Build retained-light bytes for Compact AO from a regular world XZ grid.
   Input/output each w*h, tmp w*h floats, scratch 2*max(w,h) floats; buffers
   are disjoint. Unit mapping is 255=lit. Remap grid indices for sparse models. */
int sh4der_kos_heightfield_ao(const float *heights, unsigned w, unsigned h, float cell,
                              const float sun[3], float bias, float feather, float *tmp,
                              float *scratch, uint8_t *out);
#ifdef __cplusplus
}
#endif
#endif
