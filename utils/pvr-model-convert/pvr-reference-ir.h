/* KallistiOS ##version##
   Host-side correspondence for emitted Compact strip references.
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_REFERENCE_IR_H
#define PVR_REFERENCE_IR_H

#include <dc/pvr_chunk_model.h>
#include <stdbool.h>

/* Compiler-private data, never serialized or allocated by target KOS.
   Input: triangle/corner identifies an authored occurrence, vertex is the
   expected canonical position index. Output: strip, reversed and canonical
   UV are decoded from the finished stream in RAW reference order (before the
   runtime's reversed-strip swap, clipping, or filtering).

   Position identity alone is not sufficient for later attribute lookup.
   Importers must also include auxiliary seam identity in their joining policy
   before generating these records; this map cannot recover a discarded seam. */
typedef struct pvr_reference_ir {
    size_t triangle;
    unsigned corner;
    uint16_t vertex;
    size_t strip;
    bool reversed, has_uv;
    float canonical[2];
} pvr_reference_ir_t;

/* Verify correspondence and fill decoded fields. refs must match the
   validated model's index_references count. Only ordinary one-volume strips are
   supported. All metadata and indices are preflighted before any writes;
   immutable input streams must remain alive and disjoint from refs.
   Failure preserves refs (EINVAL/overflow/unsupported/semantic errors).
   No allocation, no UV re-quantization and no model-data mutation. */
int pvr_reference_ir_resolve(const pvr_chunk_model_t *model,
                             size_t triangle_count,
                             pvr_reference_ir_t *refs, size_t count);

#endif
