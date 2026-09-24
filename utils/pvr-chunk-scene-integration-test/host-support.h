/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/

/* The portable camera builders compile beside their target apply wrappers.
   Any accidental attempt to use the SH-4 matrix register on this host fails. */
#include <dc/vector.h>
void mat_apply(const matrix_t *matrix);
