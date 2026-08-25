/* KallistiOS ##version##

   Host-test PVR declarations for compact resource binding.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_CHUNK_BINDING_DC_PVR_H
#define TEST_CHUNK_BINDING_DC_PVR_H

#include "../../../pvr-material-test/include/dc/pvr.h"

#define PVR_CMD_VERTEX     UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)

#define PVR_RAM_SIZE     UINT32_C(0x00800000)
#define PVR_RAM_INT_BASE UINT32_C(0xa4000000)
#define PVR_RAM_INT_TOP  (PVR_RAM_INT_BASE + PVR_RAM_SIZE)

#define PVR_MIPBIAS_1_25 ((pvr_mip_bias_t)5)
#define PVR_MIPBIAS_1_50 ((pvr_mip_bias_t)6)

typedef struct pvr_vertex {
    alignas(32) uint32_t flags;
    float x, y, z, u, v;
    uint32_t argb, oargb;
} pvr_vertex_t;

typedef struct pvr_vertex_pcm {
    alignas(32) uint32_t flags;
    float x, y, z;
    uint32_t argb0, argb1, d1, d2;
} pvr_vertex_pcm_t;

typedef struct pvr_vertex_tpcm {
    alignas(32) uint32_t flags;
    float x, y, z, u0, v0;
    uint32_t argb0, oargb0;
    float u1, v1;
    uint32_t argb1, oargb1, d1, d2, d3, d4;
} pvr_vertex_tpcm_t;

typedef struct pvr_modifier_vol {
    alignas(32) uint32_t flags;
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    uint32_t d1, d2, d3, d4, d5, d6;
} pvr_modifier_vol_t;

typedef struct pvr_sprite_txr {
    alignas(32) uint32_t flags;
    float ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy;
    uint32_t dummy, auv, buv, cuv;
} pvr_sprite_txr_t;

typedef enum pvr_txr_surface_format {
    PVR_TXR_SURFACE_ARGB1555 = 0,
    PVR_TXR_SURFACE_RGB565,
    PVR_TXR_SURFACE_ARGB4444,
    PVR_TXR_SURFACE_YUV422,
    PVR_TXR_SURFACE_BUMP,
    PVR_TXR_SURFACE_PAL4BPP,
    PVR_TXR_SURFACE_PAL8BPP
} pvr_txr_surface_format_t;

typedef enum pvr_txr_surface_layout {
    PVR_TXR_SURFACE_TWIDDLED = 0,
    PVR_TXR_SURFACE_LINEAR,
    PVR_TXR_SURFACE_STRIDE,
    PVR_TXR_SURFACE_VQ
} pvr_txr_surface_layout_t;

typedef struct pvr_txr_surface {
    pvr_ptr_t vram;
    size_t capacity;
    size_t byte_size;
    size_t codebook_size;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    uint16_t mip_levels;
    pvr_txr_surface_format_t format;
    pvr_txr_surface_layout_t layout;
    bool mipmapped;
    bool owns_vram;
} pvr_txr_surface_t;

typedef struct pvr_txr_level_info {
    uint32_t width;
    uint32_t height;
    size_t offset;
    size_t byte_size;
} pvr_txr_level_info_t;

typedef struct pvr_mem_reservation {
    pvr_ptr_t base;
    size_t capacity;
} pvr_mem_reservation_t;

typedef enum pvr_txr_residency_state {
    PVR_TXR_RESIDENCY_EMPTY = 0,
    PVR_TXR_RESIDENCY_LOADING,
    PVR_TXR_RESIDENCY_READY
} pvr_txr_residency_state_t;

typedef struct pvr_txr_residency_slot {
    uint32_t identifier;
    uint32_t pin_count;
    uint64_t generation;
    uint64_t last_use;
    pvr_txr_residency_state_t state;
} pvr_txr_residency_slot_t;

typedef struct pvr_txr_residency_handle {
    size_t slot;
    uint64_t generation;
} pvr_txr_residency_handle_t;

typedef struct pvr_txr_residency_status {
    size_t slot_count;
    size_t ready_slots;
    size_t loading_slots;
    size_t pinned_slots;
    uint64_t pin_count;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} pvr_txr_residency_status_t;

typedef struct pvr_txr_residency {
    pvr_mem_reservation_t reservation;
    pvr_txr_residency_slot_t *slots;
    pvr_txr_surface_t *surfaces;
    size_t slot_count;
    size_t slot_stride;
    uint64_t clock;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint32_t _magic;
} pvr_txr_residency_t;

int pvr_txr_surface_get_level(const pvr_txr_surface_t *surface,
                              uint32_t level, pvr_txr_level_info_t *info);
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *surface,
                                        pvr_ptr_t *address);
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *surface);
int pvr_txr_residency_acquire(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface);
int pvr_txr_residency_unpin(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle);
int pvr_txr_residency_get_status(const pvr_txr_residency_t *cache,
                                 pvr_txr_residency_status_t *status);

#endif /* TEST_CHUNK_BINDING_DC_PVR_H */
