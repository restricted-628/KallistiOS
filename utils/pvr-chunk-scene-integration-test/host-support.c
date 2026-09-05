/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/

#include "host-support.h"
#include <dc/pvr.h>
#include <stdlib.h>

void mat_apply(const matrix_t *matrix) {
    (void)matrix;
    abort();
}

int pvr_prim(const void *data, size_t bytes) {
    (void)data;
    (void)bytes;
    abort();
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t bytes) {
    (void)list;
    (void)data;
    (void)bytes;
    abort();
}

void pvr_mod_compile(pvr_mod_hdr_t *header, pvr_list_t list,
                     uint32_t mode, uint32_t cull) {
    (void)header;
    (void)list;
    (void)mode;
    (void)cull;
    abort();
}
