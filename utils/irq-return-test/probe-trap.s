! KallistiOS ##version##
! Copyright (C) 2026 Joseph Black
! Observe returned SR/SP immediately, before a C comparison can change SR.T.

    .text
    .align 2
    .globl _probe_trap
_probe_trap:
    stc sr,r0
    mov.l r0,@r4
    mov.l r15,@(8,r4)
    trapa #253
    stc sr,r0
    mov.l r0,@(4,r4)
    mov.l r15,@(12,r4)
    rts
    nop
