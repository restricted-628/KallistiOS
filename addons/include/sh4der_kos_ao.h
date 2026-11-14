/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_AO_H
#define KOS_SH4DER_AO_H
#include <stddef.h>
#include <stdint.h>
#include <sh4der/sh4der_ao.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Screen-cavity cue from a retained PRE-EFFECT80x60 RGB565 grid. This is not
   depth/normal AO. Caller scratch4800 bytes, output128x64 RGB565; all ranges
   disjoint. Failure leaves outputs intact. Borders/padding are neutral white.
   Inputs can be static host assets or frame_read output after producer COMPLETE.
   Bind image80x60 content, edge UVs, bilinear/clamp; upload through image helper
   after retiring prior readers; draw MULTIPLY quad via one existing TR sink.
   No capture, resources, global state or per-pixel descriptor admission. */
int sh4der_kos_ao_map(const uint16_t *grid,size_t count,uint8_t *luma,size_t scratch_count,
    uint16_t *out,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
