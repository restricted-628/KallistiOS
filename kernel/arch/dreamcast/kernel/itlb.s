! Routine to flush the ITLB cache
! This routine will probably turn out to be useless in its current
!   form, but will be required later as pages are mapped and unmapped.
! Copyright (C) 2026 Joseph Black

	.text
	.globl _mmu_reset_itlb

_mmu_reset_itlb:
	mov.l	mraddr,r0
	mov.l	p2mask,r1
	or	r1,r0
	jmp	@r0
	nop

	.align	2
mraddr:	.long	mmu_reset_real
p2mask:	.long	0x20000000
	

mmu_reset_real:
	! Clear the ITLB Address array
	mov.l	itlb1,r4
	mov	#0,r0
	mov	#1,r1
	shll16	r1
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4

	! Clear ITLB Data Array 1
	mov.l	itlb2,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4

	! Clear ITLB Data Array 2
	mov.l	itlb3,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4
	add	r1,r4
	mov.l	r0,@r4

	! make sure we have enough instrs before returning to P1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	rts
	nop

	.align	2
itlb1:	.long	0xf2000000 
itlb2:	.long	0xf3000000
itlb3:	.long	0xf3800000

! Invalidate any UTLB entry matching the supplied VPN and ASID. An
! associative UTLB address-array write also invalidates a matching ITLB
! entry. Direct TLB-array accesses are performed from P2 as required by the
! SH-4 memory-management programming contract.
	.globl _mmu_invalidate_tlb

_mmu_invalidate_tlb:
	mov.l	inval_real_addr,r0
	mov.l	inval_p2mask,r1
	or	r1,r0
	jmp	@r0
	nop

mmu_invalidate_tlb_real:
	mov.l	utlb_assoc,r0
	mov.l	vpn_mask,r1
	and	r1,r4
	extu.b	r5,r5
	or	r5,r4
	mov.l	r4,@r0

	! Keep executing in P2 long enough for the array write to settle before
	! fetching the next instruction from a translated/cached region.
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	rts
	nop

	.align	2
inval_real_addr:	.long	mmu_invalidate_tlb_real
inval_p2mask:		.long	0x20000000
utlb_assoc:		.long	0xf6000080
vpn_mask:		.long	0xfffffc00

! Rewrite the two reserved SQ UTLB entries from P2. The caller has already
! assembled the PTEL values for entries 62 and 63 in r4 and r5.
	.globl _mmu_set_sq_addr_asm

_mmu_set_sq_addr_asm:
	mov.l	sq_real_addr,r0
	mov.l	sq_p2mask,r1
	or	r1,r0
	jmp	@r0
	nop

mmu_set_sq_addr_real:
	mov.l	sq_utlb_62,r0
	mov.l	r4,@r0
	mov.l	sq_utlb_63,r0
	mov.l	r5,@r0

	! Preserve the required separation before returning to P1.
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	rts
	nop

	.align	2
sq_real_addr:	.long	mmu_set_sq_addr_real
sq_p2mask:	.long	0x20000000
sq_utlb_62:	.long	0xf7003e00
sq_utlb_63:	.long	0xf7003f00
