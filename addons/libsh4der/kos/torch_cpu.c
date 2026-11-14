/* KallistiOS ##version##
   Select only the pinned torch translation unit's CPU geometry definitions.
   SH4ZAM still selects native SH-4 through compiler __sh__/__SH__ macros.
   The included implementation retains its upstream license and provenance. */
#ifdef _arch_dreamcast
#undef _arch_dreamcast
#endif
#include "../upstream/src/sh4der_torch.c"
