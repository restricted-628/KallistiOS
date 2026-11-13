/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_FRAME_H
#define KOS_SH4DER_FRAME_H
#include <dc/pvr.h>
#include <sh4der/sh4der_frame.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SH4DER_KOS_FRAME_SAMPLES (SH4DER_FRAME_GRID_X * SH4DER_FRAME_GRID_Y)

/* Full identity, not the vendor's three-bit visible stamp. The owner supplies
   a nonzero generation, never reused across rebind, resize or PVR restart. */
typedef struct sh4der_kos_frame_id {
    uint64_t generation;
    pvr_render_id_t render;
} sh4der_kos_frame_id_t;

/* Caller storage; initialize with bind and leave immutable while admitted.
   Offsets are in RGB565 texels. No allocation, pin or implicit lifetime owner. */
typedef struct sh4der_kos_frame_binding {
    const volatile uint16_t *pixels;
    uint64_t generation;
    uint32_t width, height, stride;
    size_t x[SH4DER_FRAME_GRID_X], neighbor_x[SH4DER_FRAME_GRID_X];
    size_t row[SH4DER_FRAME_GRID_Y], neighbor_row[SH4DER_FRAME_GRID_Y];
} sh4der_kos_frame_binding_t;

/* Admit a bound nonmip linear/stride RGB565 surface once. width/height are the
   rendered region, not necessarily storage size. Precompute grid centers and
   clamped +3 neighbors. Failure leaves out intact. Inputs/output disjoint.
   Keep the underlying allocation and descriptor stable until all CPU reads
   and GPU producers/readers retire; rebind on replacement even at same address.
   An all-zero binding is invalid. Clearing it does NOT retire GPU work. */
int sh4der_kos_frame_bind(sh4der_kos_frame_binding_t *out,
    const pvr_txr_surface_t *surface, uint32_t width, uint32_t height,
    uint64_t generation);

/* Synchronous sparse CPU sample: 4800 RGB565 reads, or 9600 with dark.
   The ticket must be the LAST writer to this retained allocation, produced in
   this generation. The owner serializes reads against writes/reuse/shutdown;
   matching ticket metadata alone cannot detect a later unreported writer.
   No implicit wait: incomplete producer returns -1/EAGAIN. Other failures also
   leave raw/dark/id unchanged. capacity counts uint16_t entries in EACH output;
   dark is optional. All outputs, binding and source must be disjoint.
   Success sets id. CPU read retirement is return; it does not retire any GPU
   sampling readers. DISPLAYED framebuffer tickets are never capture sources. */
int sh4der_kos_frame_read(const sh4der_kos_frame_binding_t *binding,
    const pvr_render_ticket_t *ticket, uint64_t generation,
    uint16_t *raw, uint16_t *dark, size_t capacity, sh4der_kos_frame_id_t *id);

typedef struct sh4der_kos_frame_metadata {
    sh4der_kos_frame_id_t id;
    float gain, gain_ln, glow;
} sh4der_kos_frame_metadata_t;

/* Metadata only: these slots do not retain or release history textures. */
typedef struct sh4der_kos_frame_history {
    uint64_t generation;
    sh4der_kos_frame_metadata_t slots[SH4DER_FRAME_HISTORY];
} sh4der_kos_frame_history_t;

/* Reset on binding generation change, including resize/PVR restart. */
int sh4der_kos_frame_history_reset(sh4der_kos_frame_history_t *history,
    uint64_t generation);
/* Record metadata used to render the identified frame, not current metadata
   at readback time. Slot eviction is explicit: old IDs return ENOENT. */
int sh4der_kos_frame_history_put(sh4der_kos_frame_history_t *history,
    const sh4der_kos_frame_metadata_t *metadata);
/* Failure leaves out unchanged. Inputs/output disjoint. */
int sh4der_kos_frame_history_get(const sh4der_kos_frame_history_t *history,
    sh4der_kos_frame_id_t id, sh4der_kos_frame_metadata_t *out);
#ifdef __cplusplus
}
#endif
#endif
