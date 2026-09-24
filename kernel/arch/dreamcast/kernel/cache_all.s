! KallistiOS ##version##
! Copyright (C) 2026 Joseph Black
!
! Whole operand-cache maintenance through the non-associative address array.
! SH7091 / SH7750 layout: 512 entries; ORA reserves entries with bit 7 set.
! P2 execution and exception exclusion avoid refills during the scan.
    .text
    .globl _arch_dcache_purge_all_indexed
    .globl _arch_dcache_wback_all_indexed

_arch_dcache_purge_all_indexed:
    bra      .all_enter
    mov      #0, r4
_arch_dcache_wback_all_indexed:
    mov.l    .all_keep_tag, r4
.all_enter:
    mova     .all_p2, r0
    mov.l    .all_p2_mask, r1
    or       r1, r0
    jmp      @r0
    nop
    .align 2
.all_p2:
    stc      sr, r3
    mov.l    .all_block, r0
    or       r3, r0
    ldc      r0, sr
    mov.l    .all_ccr, r0
    mov.l    @r0, r0
    and      #32, r0
    shll8    r0
    shlr     r0
    mov      r0, r7         ! ORA -> array bit 12 (entry bit 7)
    mov.l    .all_tags, r5
    mov.l    .all_end, r2
.all_loop:
    tst      r7, r5
    bf       .all_next      ! Do not read or write OCRAM entries
    mov.l    @r5, r0
    tst      #1, r0
    bt       .all_next      ! Invalid entries have no data to retire
    and      r4, r0         ! Write-back: retain tag/V; purge: clear all
    mov.l    r0, @r5        ! Old V=U=1 writes back before replacing the tag
.all_next:
    add      #32, r5
    cmp/eq   r2, r5
    bf       .all_loop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ldc      r3, sr
    rts
    nop
    .align 2
.all_keep_tag: .long 0x1ffffc01
.all_p2_mask:  .long 0xa0000000
.all_block:    .long 0x10000000
.all_ccr:      .long 0xff00001c
.all_tags:     .long 0xf4000000
.all_end:      .long 0xf4004000
