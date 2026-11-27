/* KallistiOS ##version##

   hardware.c
   Copyright (C) 2000-2001 Megan Potter
   Copyright (C) 2013 Lawrence Sebald
*/

#include <stdbool.h>
#include <errno.h>
#include "../../../net/net_shutdown_internal.h"
#include <arch/arch.h>
#include <kos/init.h>
#include <kos/platform.h>
#include <dc/spu.h>
#include <dc/video.h>
#include <dc/cdrom.h>
#include <dc/asic.h>
#include <dc/maple.h>
#include <dc/net/broadband_adapter.h>
#include <dc/net/lan_adapter.h>
#include <dc/net/w5500_adapter.h>
#include <dc/vblank.h>

#include "spu_internal.h"
#include "../kernel/shutdown_internal.h"
#include "../sound/snd_mem_reservation.h"

static int initted = 0;
static bool hardware_shutdown_active;
static uint32_t sysmode;

#define SYSMODE_REG 0xA05F74B0

int hardware_sys_mode(int *region) {
    if(region)
        *region = sysmode & 0x0F;

    return (sysmode >> 4) & 0x0F;
}

int hardware_sys_init(void) {
    /* Setup ASIC stuff */
    asic_init();

    /* VBlank multiplexer */
    vblank_init();

    sysmode = *((volatile uint32_t *)SYSMODE_REG);
    initted = 1;

    return 0;
}


void eth_init(void) {
    /* Setup network (this won't do anything unless we enable netcore) */
    bba_init();
    la_init();
    w5500_adapter_init(NULL, true);
}

void eth_shutdown(void) {
    if(arch_net_shutdown_preflight() < 0)
        arch_net_shutdown_wait(errno, NET_SHUTDOWN_ETHERNET);
    la_shutdown();
    if(bba_shutdown() < 0)
        arch_net_shutdown_wait(errno, NET_SHUTDOWN_ETHERNET);
    w5500_adapter_shutdown();
}

KOS_INIT_FLAG_WEAK(eth_init, false);
KOS_INIT_FLAG_WEAK(eth_shutdown, false);
KOS_INIT_FLAG_WEAK(maple_init, true);
KOS_INIT_FLAG_WEAK(cdrom_init, true);
KOS_INIT_FLAG_WEAK(cdrom_shutdown, true);

static void hardware_periph_init_rest(void) {
    g2_dma_init();

    if(!KOS_PLATFORM_IS_NAOMI) {
        /* Init CD-ROM.. NOTE: NO GD-ROM SUPPORT. ONLY CDs/CDRs. */
        KOS_INIT_FLAG_CALL(cdrom_init);
    }

    /* Setup maple bus */
    KOS_INIT_FLAG_CALL(maple_init);

    /* Init video */
    vid_init(DEFAULT_VID_MODE, DEFAULT_PIXEL_MODE);

    if(!KOS_PLATFORM_IS_NAOMI)
        KOS_INIT_FLAG_CALL(eth_init);

    initted = 2;

}

void hardware_periph_init_boot(void) {
    spu_init_boot();
    hardware_periph_init_rest();
}

int hardware_periph_init(void) {
    if(snd_mem_reconfigure_begin() < 0)
        return -1;
    if(spu_init_in_transition() < 0) {
        int error = errno;
        (void)snd_mem_reconfigure_end();
        errno = error;
        return -1;
    }
    hardware_periph_init_rest();
    return snd_mem_reconfigure_end();
}

KOS_INIT_FLAG_WEAK(maple_shutdown, true);

static void hardware_shutdown_common(bool spu_retired) {
    bool standalone = !arch_shutdown_owns_wave_gate();

    if(standalone && (arch_shutdown_require_main_thread() < 0
            || snd_mem_reconfigure_begin() < 0))
        arch_wave_shutdown_wait(errno);
    if(snd_mem_reconfigure_require_owner() < 0)
        arch_wave_shutdown_wait(errno);
    if(hardware_shutdown_active)
        arch_wave_shutdown_wait(EALREADY);
    hardware_shutdown_active = true;

    if(arch_net_shutdown_preflight() < 0)
        arch_net_shutdown_wait(errno, NET_SHUTDOWN_HARDWARE);

    /* Workers and their callbacks need G2 until they return. */
    if(!spu_retired && spu_transfer_system_shutdown() < 0)
        arch_wave_shutdown_wait(errno);

    if(initted == 2) {
        if(!KOS_PLATFORM_IS_NAOMI)
            KOS_INIT_FLAG_CALL(eth_shutdown);
        KOS_INIT_FLAG_CALL(maple_shutdown);
        if(!KOS_PLATFORM_IS_NAOMI)
            KOS_INIT_FLAG_CALL(cdrom_shutdown);
        if(g2_dma_shutdown_checked() < 0)
            arch_wave_shutdown_wait(errno);
    }

    if(spu_sound_shutdown_in_transition() < 0)
        arch_wave_shutdown_wait(errno);
    if(initted == 2) {
        spu_clear_retired();
        vid_shutdown();
    }
    if(initted) {
        vblank_shutdown();
        asic_shutdown();
    }
    initted = 0;
    hardware_shutdown_active = false;

    if(standalone && snd_mem_reconfigure_end() < 0)
        arch_wave_shutdown_wait(errno);
}

void hardware_shutdown(void) {
    hardware_shutdown_common(false);
}

void hardware_shutdown_after_spu_retired(void) {
    if(!arch_shutdown_owns_wave_gate())
        arch_wave_shutdown_wait(EPERM);
    hardware_shutdown_common(true);
}
