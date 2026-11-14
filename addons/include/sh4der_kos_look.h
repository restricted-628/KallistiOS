/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_LOOK_H
#define KOS_SH4DER_LOOK_H
#include <sh4der_kos_frame.h>
#include <sh4der_kos_overlay.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sh4der_kos_look_plan {
    sh4der_look_bins_t bins;
    sh4der_look_params_t params;
} sh4der_kos_look_plan_t;
typedef struct sh4der_kos_look_state {
    sh4der_look_eye_t eye;
    sh4der_kos_frame_id_t last;
} sh4der_kos_look_state_t;
/* Host/load once. NULL params uses pinned defaults. Supported tuning: times
   [0,3600] seconds, transient/open[0,4], .4<=gain_lo<=gain_hi<=2 (pinned visual
   recommendation). Failure leaves plan unchanged. No current-frame fitting. */
int sh4der_kos_look_prepare(sh4der_kos_look_plan_t *out,const sh4der_look_params_t *params);
/* Camera cut or generation change; no resource lifetime effect. */
int sh4der_kos_look_reset(sh4der_kos_look_state_t *state,uint64_t generation);
/* Consume one retained80x60 RGB565 grid and its EXACT history metadata.
   Reject duplicate/stale/generation-mismatched IDs and evicted metadata before
   any state change. dt seconds[0,60]. Histogram is bounded4800 in64 bins.
   Uses sampled frame gain_ln, not the eye's last output (readback may lag).
   Source is before nongain grades/glow, or caller-corrected with metadata gain1.
   Recorded gain_ln must describe actual source, in[-8,8]. Reset on cuts/resize.
   out state contains next gain/gain_ln for grade and metadata of its future
   rendered frame. No frame capture, global eye or hidden history. */
int sh4der_kos_look_update(const sh4der_kos_look_plan_t *plan,
    sh4der_kos_look_state_t *state,const uint16_t *grid,size_t count,
    const sh4der_kos_frame_history_t *history,sh4der_kos_frame_id_t source,float dt);
#ifdef __cplusplus
}
#endif
#endif
