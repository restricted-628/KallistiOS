/* KallistiOS ##version##
   Rectangular portal composition through existing homogeneous clipping.
   Copyright (C) 2026 Joseph Black
*/
#include "portal-scene.h"

#include <errno.h>
#include <string.h>

static int append(portal_scene_t *scene, size_t pass,
                   const pvr_vertex_t *vertices, size_t count) {
    if(count > PORTAL_PASS_CAPACITY - scene->count[pass]) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(scene->vertices[pass] + scene->count[pass], vertices,
           count * sizeof(*vertices));
    scene->count[pass] += count;
    return 0;
}

static int quad(portal_scene_t *scene, size_t pass, float x0, float y0,
                 float x1, float y1, float z, uint32_t color,
                 const pvr_frustum_t *view) {
    const pvr_vertex_t corners[4] = {
        { .x = x0, .y = y0, .z = z, .u = 0, .v = 0, .argb = color },
        { .x = x1, .y = y0, .z = z, .u = 1, .v = 0, .argb = color },
        { .x = x0, .y = y1, .z = z, .u = 0, .v = 1, .argb = color },
        { .x = x1, .y = y1, .z = z, .u = 1, .v = 1, .argb = color }
    };
    static const unsigned indices[2][3] = { { 0, 1, 2 }, { 2, 1, 3 } };
    for(size_t triangle = 0; triangle < 2; ++triangle) {
        pvr_vertex_t input[3];
        for(size_t vertex = 0; vertex < 3; ++vertex) {
            input[vertex] = corners[indices[triangle][vertex]];
            input[vertex].flags = vertex == 2 ? PVR_CMD_VERTEX_EOL :
                                               PVR_CMD_VERTEX;
        }
        if(view) {
            pvr_vertex_t output[PVR_FRUSTUM_CLIP_MAX_VERTICES];
            pvr_frustum_clip_result_t result;
            if(pvr_frustum_clip_triangle(output, PVR_FRUSTUM_CLIP_MAX_VERTICES,
                    input, view, PVR_FRUSTUM_CLIP_ALL, &result) < 0 ||
                    append(scene, pass, output, result.output_vertices) < 0)
                return -1;
        }
        else if(append(scene, pass, input, 3) < 0)
            return -1;
    }
    return 0;
}

int portal_scene_build(portal_scene_t *scene, bool clear_depth,
                        bool restore_occluder) {
    /* Explicit column-major camera: X'=160X+320Z, Y'=160Y+240Z,
       W'=Z. The second view faces +Z, with reciprocal-W PVR depth.
       The frustum clips to the opening BEFORE division, not to tile bins. */
    static const matrix_t camera = {
        { 160, 0, 0, 0 }, { 0, 160, 0, 0 },
        { 320, 240, 1, 1 }, { 0, 0, 0, 0 }
    };
    pvr_frustum_t opening;
    if(!scene) {
        errno = EINVAL;
        return -1;
    }
    memset(scene, 0, sizeof(*scene));
    /* Intentionally not tile-aligned: nearby outside pixels must stay red. */
    if(pvr_frustum_init(&opening, &camera, 193, 129, 447, 351, 1, 16) < 0)
        return -1;

    if(clear_depth) {
        /* The first view covers the opening too. Its nearer wall depth must
           be discarded before drawing the remote view. */
        if(quad(scene, 0, 40, 40, 600, 440, 1, 0xffff0000, NULL) < 0)
            return -1;
    }
    else {
        /* Independent coverage construction: a real hole, not a clear
           emulation. This mode never placed wall depth in the opening. */
        if(quad(scene, 0, 40, 40, 600, 129, 1, 0xffff0000, NULL) < 0 ||
           quad(scene, 0, 40, 351, 600, 440, 1, 0xffff0000, NULL) < 0 ||
           quad(scene, 0, 40, 129, 193, 351, 1, 0xffff0000, NULL) < 0 ||
           quad(scene, 0, 447, 129, 600, 351, 1, 0xffff0000, NULL) < 0)
            return -1;
    }
    if(quad(scene, 0, 304, 80, 336, 400, 8, 0xff00ffff, NULL) < 0)
        return -1;

    /* A full-tile depth clear also removed the foreground bar's depth.
       Replay it before portal geometry; identical color preserves its image.
       Do not assume a rectangular color opening is a depth/stencil mask. */
    if(clear_depth && restore_occluder &&
       quad(scene, 1, 304, 80, 336, 400, 8, 0xff00ffff, NULL) < 0)
        return -1;

    /* Both remote objects extend beyond the opening. Their UVs and colors
       travel through the canonical clipper, backed by SH4ZAM on SH-4. */
    if(quad(scene, 1, -8, -6, 8, 6, 4, 0xff00ff00, &opening) < 0 ||
       quad(scene, 1, .5f, -.5f, 3, .5f, 2, 0xffff00ff, &opening) < 0)
        return -1;

    /* Preserve after the clear: a farther yellow overlay must fail against
       the remote view. Nothing assumes old main-view depth outside the hole. */
    return quad(scene, 2, 208, 144, 432, 336, .125f, 0xffffff00, NULL);
}
