
! KallistiOS ##version##
!
! arch/dreamcast/kernel/cache.s
!
! Copyright (C) 2001 Megan Potter
! Copyright (C) 2014, 2016, 2023 Ruslan Rostovtsev
! Copyright (C) 2023, 2024 Andy Barajas
! Copyright (C) 2024, 2025 Paul Cercueil
! Copyright (C) 2025 Matt Slevinsky
! Copyright (C) 2025 TapamN
! Copyright (C) 2026 Joseph Black
!
! Optimized assembler code for managing the cache.
!

    .text
    .globl _arch_icache_inval_range
    .globl _arch_icache_sync_range
    .globl _cache_write_ccr

! This routine goes through and flushes/invalidates the icache
! for a given range.
!
! r4 is starting address
! r5 is size
    .align 2
_arch_icache_inval_range:
    tst      r5, r5          ! Test if size is 0
    mov.l    iir_addr, r0

    bt       .iinval_exit    ! Exit early if no blocks to flush

    ! Reject a byte range whose final address wraps around the address space.
    mov      r5, r2
    add      #-1, r2
    mov      r4, r3
    add      r2, r3
    cmp/hs   r4, r3
    bf       .iinval_exit

    ! Match the C range helpers: both endpoints must share an address area.
    mov      r4, r2
    xor      r3, r2
    mov.l    area_mask, r1
    tst      r1, r2
    bf       .iinval_exit

    ! Cache tags and OCB* operands must name the cacheable alias. P2 carries
    ! the same physical address, but cache-control operations against it are
    ! ineffective.
    mov      r4, r2
    mov.l    area_mask, r1
    and      r1, r2
    mov.l    p2_mask, r1
    cmp/eq   r1, r2
    bf       .iinval_alias_done
    mov.l    cache_mask, r1
    and      r1, r4
    mov.l    p1_base, r1
    or       r1, r4
.iinval_alias_done:

    mov.l    p2_mask, r1
    or       r1, r0
    jmp      @r0
    nop

.iinval_real:
    ! Save old SR and disable interrupts
    stc      sr, r0
    mov.l    r0, @-r15
    mov.l    ormask, r1
    or       r1, r0
    ldc      r0, sr

    ! Compute the inclusive final cache line and align both endpoints.
    add      #-1, r5
    add      r4, r5
    mov.l    align_mask, r0
    and      r0, r4
    and      r0, r5
    mov.l    ica_addr, r1
    mov.l    ic_entry_mask, r2
    mov.l    ic_valid_mask, r3

    .align 2
.iinval_loop:
    ! Invalidate I cache
    mov      r4, r6
    mov      r6, r7
    and      r2, r6        ! v & CACHE_IC_ENTRY_MASK
    or       r1, r6        ! CACHE_IC_ADDRESS_ARRAY | (v & CACHE_IC_ENTRY_MASK)
    and      r3, r7        ! v & 0xfffffc00
    mov.l    r7, @r6       ! Invalidate cache entry
    cmp/eq   r5, r4
    bt       .iinval_done
    bra      .iinval_loop
    add      #32, r4       ! Move on to next cache block

.iinval_done:
    ! make sure we have enough instrs before returning to P1
    nop
    nop
    nop
    nop
    nop
    nop
    nop

    ! Restore old SR
    mov.l    @r15+, r0
    ldc      r0, sr

.iinval_exit:
    rts
    nop

! This routine goes through and flushes/invalidates the icache
! for a given range.
!
! r4 is starting address
! r5 is size
    .align 2
_arch_icache_sync_range:
    tst      r5, r5          ! Test if size is 0
    mov.l    ifr_addr, r0

    bt       .iflush_exit    ! Exit early if no blocks to flush

    ! Reject a byte range whose final address wraps around the address space.
    mov      r5, r2
    add      #-1, r2
    mov      r4, r3
    add      r2, r3
    cmp/hs   r4, r3
    bf       .iflush_exit

    ! Reject cross-area ranges before normalizing a P2 starting address.
    mov      r4, r2
    xor      r3, r2
    mov.l    area_mask, r1
    tst      r1, r2
    bf       .iflush_exit

    ! Normalize a direct P2 alias before both the data-cache write-back and
    ! instruction-cache tag invalidation.
    mov      r4, r2
    mov.l    area_mask, r1
    and      r1, r2
    mov.l    p2_mask, r1
    cmp/eq   r1, r2
    bf       .iflush_alias_done
    mov.l    cache_mask, r1
    and      r1, r4
    mov.l    p1_base, r1
    or       r1, r4
.iflush_alias_done:

    mov.l    p2_mask, r1

    or       r1, r0
    jmp      @r0
    nop

.iflush_real:
    ! Save old SR and disable interrupts
    stc      sr, r0
    mov.l    r0, @-r15
    mov.l    ormask, r1
    or       r1, r0
    ldc      r0, sr

    ! Compute the inclusive final cache line and align both endpoints.
    add      #-1, r5
    add      r4, r5
    mov.l    align_mask, r0
    and      r0, r4
    and      r0, r5
    mov.l    ica_addr, r1
    mov.l    ic_entry_mask, r2
    mov.l    ic_valid_mask, r3

.iflush_loop:
    ! Invalidate I cache
    mov      r4, r6
    mov      r6, r7
    and      r2, r6        ! v & CACHE_IC_ENTRY_MASK
    or       r1, r6        ! CACHE_IC_ADDRESS_ARRAY | (v & CACHE_IC_ENTRY_MASK)
    ocbwb    @r7           ! Write back D cache
    and      r3, r7        ! v & 0xfffffc00
    mov.l    r7, @r6       ! Invalidate cache entry
    cmp/eq   r5, r4
    bt       .iflush_done
    bra      .iflush_loop
    add      #32, r4       ! Move on to next cache block

.iflush_done:
    ! make sure we have enough instrs before returning to P1
    nop
    nop
    nop
    nop
    nop
    nop
    nop

    ! Restore old SR
    mov.l    @r15+, r0
    ldc      r0, sr

.iflush_exit:
    rts
    nop

_cache_write_ccr:
    ! Exclude competing updates before reading the old configuration.
    stc      sr, r7
    mov.l    block_bit, r0
    or       r7, r0
    ldc      r0, sr

    ! Run the entire register transaction and retirement from P2.
    mova     .ccr_p2, r0
    mov.l    p2_mask, r1
    or       r1, r0
    jmp      @r0
    nop
    .align 2
.ccr_p2:
    mov.l    ccr_addr, r6
    mov.l    @r6, r0
    mov      r0, r3         ! Old layout determines which entries hold data

    ! Preserve the existing API: new = (old & ~mask) | value.
    or       r4, r0
    xor      r4, r0
    or       r5, r0
    mov      r0, r4

    mov      r3, r0
    and      #32, r0
    shll8    r0
    shlr     r0
    mov      r0, r3         ! Old ORA -> array bit 12 (entry bit 7)

    ! Retire dirty cache lines before changing their interpretation. Never
    ! access the address-array entries belonging to an active OCRAM bank.
    mov.l    loc_tags, r0
    mov      #2, r1
    shll8    r1
    mov      #0, r2
.ccr_loop:
    tst      r3, r0
    bf       .ccr_next
    mov.l    r2, @r0        ! Non-associative: old dirty valid data writes back
.ccr_next:
    dt       r1
    add      #32, r0
    bf       .ccr_loop

    ! OCI clears tags, not scratchpad data. After retirement it also prevents
    ! former OCRAM tags becoming live cache entries when ORA is cleared.
    mov      r4, r0
    or       #8, r0
    mov.l    r0, @r6

    ! Preserve the CCR update-to-cached-access/return separation.
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ldc      r7, sr
    rts
    nop

! Variables
    .align    2

! I-cache (Instruction cache)
ica_addr:
    .long    0xf0000000    ! icache array address
ic_entry_mask:
    .long    0x1fe0        ! CACHE_IC_ENTRY_MASK
ic_valid_mask:
    .long    0xfffffc00
ifr_addr:
    .long    .iflush_real
iir_addr:
    .long    .iinval_real
p2_mask:
    .long    0xa0000000
area_mask:
    .long    0xe0000000
cache_mask:
    .long    0x1fffffff
p1_base:
    .long    0x80000000
ormask:
    .long    0x100000f0
align_mask:
    .long    ~31           ! Align address to 32-byte boundary
ccr_addr:
    .long    0xff00001c    ! Cache control register
block_bit:
    .long    0x10000000
loc_tags:
    .long    0xf4000000
