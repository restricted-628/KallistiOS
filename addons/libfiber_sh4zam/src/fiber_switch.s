! KallistiOS ##version##
!
!   arch/dreamcast/kernel/fiber_switch.s
!   Copyright (C) 2026 Joseph Black
!
! Cooperative SH-4 context transfer at a normal C call boundary. Fibers share
! their owning KOS thread's MMU context, GBR/TLS, newlib state, and kernel wait
! identity. Only the nonvolatile ABI registers, stack, return continuation, and
! interrupt mask are transferred here.

	.text
	.balign	4
	.globl	_arch_fiber_context_switch

! r4 = outgoing arch_fiber_context_t
! r5 = incoming arch_fiber_context_t
_arch_fiber_context_switch:
	stc	sr,r0
	mov.l	r8,@(0,r4)
	mov.l	r9,@(4,r4)
	mov.l	r10,@(8,r4)
	mov.l	r11,@(12,r4)
	mov.l	r12,@(16,r4)
	mov.l	r13,@(20,r4)
	mov.l	r14,@(24,r4)
	mov.l	r15,@(28,r4)

	mov	r4,r1
	add	#32,r1
	fmov.s	fr12,@r1
	add	#4,r1
	fmov.s	fr13,@r1
	add	#4,r1
	fmov.s	fr14,@r1
	add	#4,r1
	fmov.s	fr15,@r1

	sts	pr,r1
	mov.l	r1,@(48,r4)
	mov.l	r0,@(52,r4)

	mov	r5,r1
	add	#32,r1
	fmov.s	@r1,fr12
	add	#4,r1
	fmov.s	@r1,fr13
	add	#4,r1
	fmov.s	@r1,fr14
	add	#4,r1
	fmov.s	@r1,fr15

	mov.l	@(48,r5),r0
	lds	r0,pr
	mov.l	@(52,r5),r1
	mov.l	@(28,r5),r2
	mov.l	@(0,r5),r8
	mov.l	@(4,r5),r9
	mov.l	@(8,r5),r10
	mov.l	@(12,r5),r11
	mov.l	@(16,r5),r12
	mov.l	@(20,r5),r13
	mov.l	@(24,r5),r14

	! Install the incoming stack before restoring its interrupt mask. No
	! interrupt may observe the incoming continuation with the outgoing stack.
	mov	r2,r15
	ldc	r1,sr
	rts
	nop
