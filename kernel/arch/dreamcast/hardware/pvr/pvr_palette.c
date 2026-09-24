/* KallistiOS ##version##

   pvr_palette.c
   (C)2002 Megan Potter
   Copyright (C) 2026 Joseph Black

 */

#include <errno.h>
#include <dc/pvr.h>
#include "pvr_internal.h"

/*
   In addition to its 16-bit truecolor modes, the PVR also supports some
   nice paletted modes. These aren't useful for super high quality images
   most of the time, but they can be useful for doing some interesting
   special effects, like the old cheap "worm hole".
*/

/* Set the palette format */
void pvr_set_pal_format(pvr_palfmt_t fmt) {
    PVR_SET(PVR_PALETTE_CFG, fmt);
}

int pvr_set_pal_entries(uint32_t first, const uint32_t *values, size_t count) {
    size_t i;

    if(first >= 1024 || count > 1024 - first || (count && !values)) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    /* Palette entries are hardware-visible independently. Avoid disabling
       interrupts for a potentially full-table update; callers that need a
       frame-atomic change should wait for rendering to finish first. */
    for(i = 0; i < count; ++i)
        PVR_SET(PVR_PALETTE_TABLE_BASE + 4 * (first + i), values[i]);

    return 0;
}
