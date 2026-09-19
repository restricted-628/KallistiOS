/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_GEOMETRY_INTERNAL_H
#define PVR_GEOMETRY_INTERNAL_H

#include <dc/pvr_geometry.h>

/* Internal admitted-cache path only. Caller has checked the finite matrix,
   aligned packed workspace range, count/capacity and disjointness, and has
   assigned canonical vertex commands. Checks changing positions/projected
   results and preserves XMTRX on both success and failure. No sink publication
   occurs here; workspace may contain a transformed prefix on failure. */
int pvr_geometry_project_canonical_inplace(pvr_vertex_t *vertices,
                                           size_t count,
                                           const matrix_t *matrix);

#endif
