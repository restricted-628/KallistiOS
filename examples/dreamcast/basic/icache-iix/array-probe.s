! KallistiOS ##version##
! Copyright (C) 2026 Joseph Black
! Test-only P2 harness. All synthetic IC tags are cleared before returning.
    .text
    .globl _icache_array_probe
    .align 2
_icache_array_probe:
    mov.l .real_addr,r0
    mov.l .p2_mask,r1
    or r1,r0
    jmp @r0
    nop
.real:
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov r4,r8
    mov r5,r9
    mov r6,r10
    mov r7,r11
    stc sr,r13
    mov.l .block,r0
    or r13,r0
    ldc r0,sr
    mov.l .ccr,r6
    mov.l @r6,r12
    mov r12,r0
    mov.l .clear_iix,r1
    and r1,r0
    mov r10,r2
    mov.l .iix,r1
    and r1,r2
    or r2,r0
    mov.l .ici,r1
    or r1,r0
    mov.l r0,@r6
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .array,r4
    mov.l .end,r5
    mov.l .seed,r0
.seed_loop:
    mov.l r0,@r4
    add #32,r4
    cmp/eq r5,r4
    bf .seed_loop
    ! Public functions are entered through P2 so changing IIX and seeding
    ! tags cannot cause execution from a synthetic instruction-cache entry.
    mov r10,r0
    tst #1,r0
    bt .inval
    mov.l .sync_fn,r0
    bra .call
    nop
.inval:
    mov.l .inval_fn,r0
.call:
    mov.l .p2_mask,r1
    or r1,r0
    mov r8,r4
    jsr @r0
    mov r9,r5
    mov.l .array,r4
    mov.l .end,r5
.read_loop:
    mov.l @r4,r0
    mov.l r0,@r11
    add #4,r11
    add #32,r4
    cmp/eq r5,r4
    bf .read_loop
    ! Discard all test tags while restoring the original cache mode.
    mov.l .ccr,r6
    mov.l .ici,r0
    or r12,r0
    mov.l r0,@r6
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ldc r13,sr
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .align 2
.real_addr: .long .real
.p2_mask: .long 0x20000000
.block: .long 0x10000000
.ccr: .long 0xff00001c
.clear_iix: .long 0xffff7fff
.iix: .long 0x8000
.ici: .long 0x800
.array: .long 0xf0000000
.end: .long 0xf0002000
.seed: .long 0x0c001001
.sync_fn: .long _arch_icache_sync_range
.inval_fn: .long _arch_icache_inval_range
