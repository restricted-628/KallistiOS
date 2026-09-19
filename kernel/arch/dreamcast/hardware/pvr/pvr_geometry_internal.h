/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_GEOMETRY_INTERNAL_H
#define PVR_GEOMETRY_INTERNAL_H

#include <dc/pvr_geometry.h>

/* Internal admitted-cache path only. Caller has checked the finite matrix,
   aligned packed workspace range, count/capacity and disjointness, and has
   assigned vertex commands. Stride must be 32 or 64 bytes, for canonical or
   two-volume packets whose XYZ fields start at byte 4 (not sprites/modifiers).
   Checks changing positions/projected
   results and preserves XMTRX on both success and failure. No sink publication
   occurs here; workspace may contain a transformed prefix on failure. */
int pvr_geometry_project_packed_inplace(void *vertices, size_t count,
                                       size_t stride, const matrix_t *matrix);

/* Same admitted preconditions, but for one 64-byte, three-corner modifier
   packet. Checks all corners before changing XYZ; leaves other fields intact.
   Preserves XMTRX on success and failure. Does not publish to a sink. */
int pvr_geometry_project_modifier_inplace(pvr_modifier_vol_t *triangle,
                                         const matrix_t *matrix);

#endif
