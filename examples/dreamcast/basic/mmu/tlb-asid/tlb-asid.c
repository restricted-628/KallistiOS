/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Seed real UTLB/ITLB array entries, retire an inactive ASID, and inspect
   validity. No translated data access is required or claimed by this probe.
*/
#include <kos.h>
#include <arch/mmu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_MMU);

/* Internal kernel helper under test, not a new public MMU API. */
extern void mmu_invalidate_tlb(uint32_t virt, uint32_t asid);
extern void tlb_probe_write(uint32_t address, uint32_t value);
extern uint32_t tlb_probe_read(uint32_t address);

#define VPN 0x10000000u
#define PHYS 0x0c010000u
#define UADDR 0xf6000000u
#define UDATA 0xf7000000u
#define IADDR 0xf2000000u
#define IDATA 0xf3000000u
#define VALID 0x100u
#define ACTIVE 3u
static volatile uint32_t *const pteh = (volatile uint32_t *)0xff000000;

enum action { DIRECT, REMAP, CACHE, UNMAP, DESTROY };

static void clear_entries(void) {
    for(unsigned i = 0; i < 2; ++i) {
        tlb_probe_write(UADDR + i * 256, 0);
        tlb_probe_write(IADDR + i * 256, 0);
    }
}

static void seed(unsigned entry, unsigned asid, uint32_t vpn,
                 bool shared, bool itlb_only) {
    /* 4 KiB, valid, uncached; private unless testing a shared translation. */
    uint32_t data = PHYS | VALID | 0x10u | (shared ? 2u : 0u);
    tlb_probe_write(UDATA + entry * 256, data | 0x64u);
    tlb_probe_write(UADDR + entry * 256,
                    vpn | asid | (itlb_only ? 0u : VALID | 0x200u));
    tlb_probe_write(IDATA + entry * 256, data | 0x40u);
    tlb_probe_write(IADDR + entry * 256, vpn | asid | VALID);
}

static bool run_case(unsigned asid, enum action action,
                     bool shared, bool itlb_only) {
    mmucontext_t *context = NULL;
    bool ok = true;
    if(action != DIRECT) {
        context = mmu_context_create(asid);
        if(!context)
            return false;
        if(mmu_page_map_ex(context, VPN >> 12, PHYS >> 12, 1,
                           MMU_ALL_RDWR, MMU_NO_CACHE, false, true) < 0) {
            mmu_context_destroy(context);
            return false;
        }
    }

    unsigned int irq = irq_disable();
    uint32_t saved_pteh = *pteh;
    const unsigned peer = asid == ACTIVE ? ACTIVE + 1 : ACTIVE;
    clear_entries();
    seed(0, asid, VPN, shared, itlb_only);
    seed(1, peer, shared ? VPN + 4096 : VPN, false, false);
    uint32_t u0 = tlb_probe_read(UADDR), i0 = tlb_probe_read(IADDR);
    uint32_t u1 = tlb_probe_read(UADDR + 256), i1 = tlb_probe_read(IADDR + 256);
    ok &= !!(u0 & VALID) == !itlb_only && (i0 & VALID);
    ok &= (u1 & VALID) && (i1 & VALID);
    *pteh = 0x07654000u | ACTIVE;

    switch(action) {
        case DIRECT: mmu_invalidate_tlb(VPN, asid); break;
        case REMAP:
            ok &= mmu_page_map_ex(context, VPN >> 12, (PHYS >> 12) + 1,
                                  1, MMU_ALL_RDWR, MMU_NO_CACHE, false, true) == 0;
            break;
        case CACHE:
            /* Same policy still retires the mapping; no physical cache scan. */
            ok &= mmu_page_set_cache(context, VPN >> 12, 1, MMU_NO_CACHE) == 0;
            break;
        case UNMAP: ok &= mmu_page_unmap(context, VPN >> 12, 1) == 0; break;
        case DESTROY: mmu_context_destroy(context); context = NULL; break;
    }
    ok &= *pteh == (0x07654000u | ACTIVE);
    uint32_t after_u0 = tlb_probe_read(UADDR), after_i0 = tlb_probe_read(IADDR);
    ok &= !(after_u0 & VALID) && !(after_i0 & VALID);
    /* Ignore reserved read bits; VPN, ASID, D/V of the peer must survive. */
    ok &= tlb_probe_read(UADDR + 256) == u1;
    ok &= ((tlb_probe_read(IADDR + 256) ^ i1) & 0xfffffdffu) == 0;
    clear_entries();
    *pteh = saved_pteh;
    irq_restore(irq);

    if(context)
        mmu_context_destroy(context);
    if(!ok)
        printf("TLB-ASID: FAIL asid=%u action=%u shared=%u itlb_only=%u "
               "before=%08lx/%08lx after=%08lx/%08lx\n", asid, action,
               shared, itlb_only, (unsigned long)u0, (unsigned long)i0,
               (unsigned long)after_u0, (unsigned long)after_i0);
    return ok;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    mmu_init();
    if(*(volatile uint32_t *)0xff000010 & 0x100u) {
        puts("TLB-ASID: FAIL requires KOS multiple-virtual mode (SV=0)");
        mmu_shutdown();
        return EXIT_FAILURE;
    }
    mmucontext_t *active = mmu_context_create(ACTIVE);
    if(!active) {
        mmu_shutdown();
        return EXIT_FAILURE;
    }
    mmu_use_table(active);
    mmu_switch_context(active);
    bool ok = true;
    const unsigned ids[] = {0, 7, 255, ACTIVE};
    for(unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        ok &= run_case(ids[i], DIRECT, false, false);
    ok &= run_case(7, DIRECT, false, true);
    ok &= run_case(7, DIRECT, true, false);
    for(enum action action = REMAP; action <= DESTROY; ++action)
        ok &= run_case(7, action, false, false);
    ok &= mmu_cxt_current == active && (*pteh & 255u) == ACTIVE;
    mmu_context_destroy(active);
    mmu_shutdown();
    if(ok)
        puts("TLB-ASID: PASS cases=10 inactive=1 peer=1 itlb=1 lifecycle=4 restore=1");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
