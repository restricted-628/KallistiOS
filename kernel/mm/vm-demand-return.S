/* Private opt-in DP1 bridge; explicitly linked only by experiments. */
.text
.balign 4
.global _vm_demand_return
_vm_demand_return:
 stc sr,r0
 mov.l .mask,r1
 or r1,r0
 ldc r0,sr
 mov r5,r2
 mov #64,r6
1:
 mov.l @r4+,r0
 mov.l r0,@r5
 add #4,r5
 dt r6
 bf 1b
 mov.l .slot,r0
 mov.l r2,@r0
 mov.l .finish,r0
 jmp @r0
 nop
.balign 4
.mask: .long 0x100000f0
.slot: .long _irq_srt_addr
.finish: .long _irq_force_return
.global _vm_demand_stop
_vm_demand_stop:
 stc sr,r0
 mov.l .stop_mask,r1
 or r1,r0
 ldc r0,sr
 mov.l .watchdog,r0
 jsr @r0
 nop
2:
 sleep
 bra 2b
 nop
.balign 4
.stop_mask: .long 0x100000f0
.watchdog: .long _wdt_disable
