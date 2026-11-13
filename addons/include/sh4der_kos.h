/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. See libsh4der/README.md for dependencies. */
#ifndef KOS_SH4DER_H
#define KOS_SH4DER_H
#include <dc/pvr_chunk_render.h>
#include <sh4der/sh4der_blade.h>
#include <sh4der/sh4der_holo.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Indexed by actual model index, including holes. Arrays are optional at bind;
   selecting holo later requires non-NULL holo_weights in this binding.
   AO is a retained-light multiplier, 0..255. Bone identifiers exclude a limb
   from its own shadow; absent identifiers mean SH4DOW_BONE_NONE. */
typedef struct sh4der_kos_sidecars {
    const uint8_t *holo_weights;
    const uint8_t *ao;
    const uint16_t *bones;
    size_t count;
} sh4der_kos_sidecars_t;

typedef struct sh4der_kos_binding {
    const pvr_chunk_model_view_t *view;
    sh4der_kos_sidecars_t sidecars;
} sh4der_kos_binding_t;

/* Bind once to an already admitted immutable view, without rescanning it.
   Failure preserves out. Rebind on model/sidecar replacement, including reuse
   at the same address. View and sidecars must outlive every callback using them.
   Do not change a binding while in use. This does not own model storage. */
int sh4der_kos_bind(sh4der_kos_binding_t *out,
                    const pvr_chunk_model_view_t *view,
                    const sh4der_kos_sidecars_t *sidecars);

typedef enum sh4der_kos_color_mode {
    SH4DER_KOS_REPLACE, SH4DER_KOS_MODULATE, SH4DER_KOS_ADD
} sh4der_kos_color_mode_t;

/* Caller-owned per-draw state. Select at most one of blade/holo. NULL means
   only AO/shadow. Holo requires binding->sidecars.holo_weights, admitted for
   this exact model/index extent; use this binding only with that model.
   mode must be one of the three declared enum values. blade_rgb must contain
   finite [0,1] values. Frames must be initialized by successful vendor prepare
   calls (or equivalent validated construction) and kept in their documented
   finite domains. In particular shadow->dark and evaluated shadow strength
   are [0,1], so retained light and final channel values remain bounded.
   Positions after base_prepare must be finite. These draw-state preconditions
   are established once by the owner; the callback does not check them again.
   Invoke base_prepare first for existing lighting/deformation;
   never point it back at sh4der_kos_vertex. Effects use the resulting vertex
   position. Prepared shadow limbs/light/axes MUST be in that same space.
   Shadow axes = sh4dow_axes(shadow), refreshed when shadow changes. No XMTRX
   mutation, allocation, submission, list or material selection occurs here.
   AO/shadows attenuate RGB after effect composition; alpha/offset color stay
   unchanged except REPLACE takes the effect alpha. Frames remain immutable
   during a draw; regenerate dynamic data at its natural update boundary. */
typedef struct sh4der_kos_effect {
    const sh4der_kos_binding_t *binding;
    const sh4der_blade_frame_t *blade;
    const sh4der_holo_frame_t *holo;
    const sh4dow_frame_t *shadow;
    sh4dow_axes_t shadow_axes;
    float blade_rgb[3];
    uint8_t blade_alpha;
    sh4der_kos_color_mode_t mode;
    pvr_chunk_render_prepare_vertex_t base_prepare;
    void *base_data;
} sh4der_kos_effect_t;

/* Direct Compact prepare_vertex callback. Preconditions above are admitted by
   the caller, not repeatedly checked per vertex. Same binding/model required. */
int sh4der_kos_vertex(const pvr_chunk_render_state_t *state,
                      const pvr_chunk_vertex_attributes_t *attributes,
                      const pvr_chunk_strip_attributes_t *strip,
                      pvr_vertex_t *vertex, void *data);
#ifdef __cplusplus
}
#endif
#endif
