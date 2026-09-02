/* KallistiOS ##version##

   Compact-model material pass routing.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_render.h>

#include <errno.h>

int pvr_chunk_render_state_list(const pvr_chunk_render_state_t *state,
                                pvr_list_t *list) {
    pvr_blend_mode_t source = PVR_BLEND_ONE;
    pvr_blend_mode_t destination = PVR_BLEND_ZERO;
    pvr_list_t resolved;

    if(!state || !list) {
        errno = EINVAL;
        return -1;
    }
    if(state->strip_flags & ~PVR_CHUNK_STRIP_FLAGS_MASK) {
        errno = EILSEQ;
        return -1;
    }
    if(state->present & PVR_CHUNK_RENDER_BLEND) {
        source = state->blend_source;
        destination = state->blend_destination;
        if(source < PVR_BLEND_ZERO || source > PVR_BLEND_INVDESTALPHA ||
           destination < PVR_BLEND_ZERO ||
           destination > PVR_BLEND_INVDESTALPHA) {
            errno = EILSEQ;
            return -1;
        }
    }

    if(source != PVR_BLEND_ONE || destination != PVR_BLEND_ZERO)
        resolved = PVR_LIST_TR_POLY;
    else if(state->strip_flags & PVR_CHUNK_STRIP_USE_ALPHA)
        resolved = PVR_LIST_PT_POLY;
    else
        resolved = PVR_LIST_OP_POLY;
    *list = resolved;
    return 0;
}
