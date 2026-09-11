/* KallistiOS ##version##
   Shared example geometry, not a public scene-graph API.
   Copyright (C) 2026 Joseph Black
*/
#ifndef PORTAL_SCENE_H
#define PORTAL_SCENE_H

#include <dc/pvr_frustum.h>
#include <stdbool.h>
#include <stddef.h>

#define PORTAL_PASS_COUNT 3u
#define PORTAL_PASS_CAPACITY 64u

typedef struct portal_scene {
    pvr_vertex_t vertices[PORTAL_PASS_COUNT][PORTAL_PASS_CAPACITY];
    size_t count[PORTAL_PASS_COUNT];
} portal_scene_t;

/* Build once into caller-owned memory. The false restore_occluder variant
   exists for the host test's negative control, not as a rendering preset. */
int portal_scene_build(portal_scene_t *scene, bool clear_depth,
                        bool restore_occluder);

#endif
