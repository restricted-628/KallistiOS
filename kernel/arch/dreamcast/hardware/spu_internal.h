/* KallistiOS ##version##

   spu_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __DC_SPU_INTERNAL_H
#define __DC_SPU_INTERNAL_H

void spu_transfer_system_init(void);
int spu_transfer_system_shutdown(void);

void spu_init_boot(void);
int spu_init_in_transition(void);
int spu_sound_shutdown_in_transition(void);
/* Caller owns the transition and has retired SPU and applicable DMA users. */
void spu_clear_retired(void);

#endif /* __DC_SPU_INTERNAL_H */
