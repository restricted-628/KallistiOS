
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
    .globl _mmu_purge_phys_page
    .globl _arch_dcache_purge_all_indexed
    .globl _arch_dcache_wback_all_indexed

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

! Retire a 4 KiB physical page regardless of virtual index or active TLB/ASID.
! Internal MMU lifecycle helper: r4 is a page-aligned physical address.
! Physical tag [28:12] identifies the page; tag [11:10] and index [9:5]
! identify its lines. Do not synthesize a P1 OCBP operand: its virtual color
! can differ. Clearing V/U through the non-associative array writes dirty data
! back using its physical tag. Never touch entries assigned to OCRAM.
    .align 2
_arch_dcache_wback_all_indexed:
    mov      #-1, r4        ! No page filter
    bra      .mpp_enter
    mov      #2, r7         ! Write-back only (preserve valid/tag)
    .align 2
_arch_dcache_purge_all_indexed:
    bra      _mmu_purge_phys_page
    mov      #-1, r4        ! No page filter
    .align 2
_mmu_purge_phys_page:
    mov      #0, r7         ! Purge (clear valid/tag)
.mpp_enter:
    mova     .mpp_p2, r0
    mov.l    p2_mask, r1
    or       r1, r0
    jmp      @r0
    nop
    .align 2
.mpp_p2:
    stc      sr, r3
    mov.l    block_bit, r0
    or       r3, r0
    ldc      r0, sr
    mov.l    ccr_addr, r0
    mov.l    @r0, r0
    and      #32, r0
    shll8    r0
    shlr     r0             ! ORA -> address-array bit 12 (entry bit 7)
    or       r0, r7
    mov.l    .mpp_page_mask, r1
    mov.l    loc_tags, r5
    mov.l    .mpp_end, r2
.mpp_loop:
    tst      r7, r5
    bf       .mpp_next
    mov.l    @r5, r0
    tst      #1, r0         ! Valid?
    bt       .mpp_next
    mov      #-1, r6
    cmp/eq   r4, r6
    bt       .mpp_purge
    and      r1, r0
    cmp/eq   r4, r0
    bf       .mpp_next
.mpp_purge:
    mov      #2, r6
    tst      r6, r7
    bt       .mpp_clear
    mov.l    .mpp_tag_mask, r6
    bra      .mpp_write
    and      r6, r0         ! Preserve tag/V, clear U and reserved bits
.mpp_clear:
    mov      #0, r0
.mpp_write:
    mov.l    r0, @r5        ! Non-associative purge, preserving dirty data
.mpp_next:
    add      #32, r5
    cmp/eq   r2, r5
    bf       .mpp_loop
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
.mpp_page_mask:
    .long    0x1ffff000
.mpp_tag_mask:
    .long    0x1ffffc01
.mpp_end:
    .long    0xf4004000

_cache_write_ccr:
    mov.l    ccr_addr, r6
    mov.l    @r6, r0

    ! Clear mask
    or       r4, r0
    xor      r4, r0

    ! Set bits
    or       r5, r0

    mov      r0, r4

    !Block IRQs
    mov.l    block_bit, r0
    stc      sr, r7
    or       r7, r0
    ldc      r0, sr

    !Jump to uncached P2 area before writing CCN
    mova     1f, r0
    mov      #0xa0, r1
    shll16   r1
    shll8    r1
    or       r1, r0
    jmp      @r0
    nop

.align 2
1:
    !Flush and invalidate data cache
    mov.l    loc_tags, r0
    mov      #2, r1  ! 512 >> 8 = 2
    shll8    r1
    mov      #0, r2
1:
    mov.l    r2, @r0
    dt       r1
    add      #32, r0   ! cache_line_size is 32
    bf       1b

    !Write to CCR
    mov.l    r4, @r6

    !Can't touch cache for a while after writing CCR
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop

    !Restore SR (unblock IRQs)
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
