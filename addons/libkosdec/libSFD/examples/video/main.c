/* Standalone bounded VIDEO ONLY example; no proprietary source or assets. */
#include <kos.h>
#include <dc/maple/controller.h>
#include <dc/cache.h>
#include <arch/mmu.h>
#include <stdlib.h>
#include <kosdec/sfd.h>
#include <kosdec/sfd_video.h>
#include <kosdec/sfd_mpeg.h>
#include "movie_data.h"

#ifndef CAPTURE_HOLD_MS
#define CAPTURE_HOLD_MS 0
#endif
#ifndef MEDIA_OIX
#define MEDIA_OIX 1
#endif
KOS_INIT_FLAGS(INIT_DEFAULT);

#if MEDIA_OIX
static uint32_t startup_ccr, startup_mcr, startup_bcr1;
enum address_route { ADDRESS_UNSUPPORTED, ADDRESS_PHYSICAL, ADDRESS_TRANSLATED };
static enum address_route oix_route;
/* MCR.SZ=00 is the 64-bit SDRAM bus; AMXEXT is immaterial for AMX2/3/4.
   Read the controller configuration, never infer it from installed RAM size. */
static enum address_route select_address_route(uint32_t mcr) {
    if((mcr >> 7) & 3u) return ADDRESS_UNSUPPORTED;
    switch((mcr >> 3) & 7u) {
        case 2: case 4: return ADDRESS_PHYSICAL;
        case 3: return ADDRESS_TRANSLATED;
        default: return ADDRESS_UNSUPPORTED;
    }
}
/* Size validates the controller profile after AMX selects the route. */
static bool address_profile_supported(uint32_t bcr1, uint32_t mcr, size_t ram) {
    unsigned dram_type = (bcr1 >> 2) & 7u;
    if(dram_type != 2 && dram_type != 3) return false;
    if(select_address_route(mcr) == ADDRESS_UNSUPPORTED) return false;
    if(ram != 0x01000000u && ram != 0x02000000u) return false;
    return ((mcr >> 3) & 7u) != 4 || ram == 0x01000000u;
}
static bool oix_owned, workspace_live;
static int oix_admission;
static void media_mmu_start(void) {
    /* Fresh standalone launch only; not a foreign/inherited MMU owner. */
    bool fresh = !mmu_enabled() && !mmu_cxt_current;
    startup_mcr = *(volatile uint32_t *)0xff800014;
    startup_bcr1 = *(volatile uint32_t *)0xff800000;
    oix_route = select_address_route(startup_mcr);
    if(((startup_bcr1 >> 2) & 7u) != 2 && ((startup_bcr1 >> 2) & 7u) != 3)
        oix_route = ADDRESS_UNSUPPORTED;
    arch_init_mmu();
    startup_ccr = *(volatile uint32_t *)0xff00001c;
    if(oix_route == ADDRESS_UNSUPPORTED || !fresh || !mmu_enabled() || mmu_cxt_current ||
       (*(volatile uint32_t *)0xff000000 & 255) ||
       (startup_ccr & (CCR_ORA | CCR_OIX)) || !(startup_ccr & CCR_OCE)) {
        oix_admission = -1; return;
    }
    /* Before KOS threads/peripherals start; loader must already be quiescent. */
    cache_write_ccr(CCR_OIX, CCR_OIX);
    oix_owned = true;
    oix_admission = 1;
}
static void media_mmu_stop(void) {
    /* Normal KOS shutdown reaches here after device/thread retirement. */
    if(workspace_live) for(;;) {} /* Never force release of a live borrower. */
    arch_shutdown_mmu();
    if(oix_owned) cache_write_ccr(CCR_OIX, startup_ccr & CCR_OIX);
}
static void media_early(void) {
    /* This executes before BSS clearing: initialized pointer assignments only. */
    arch_init_mmu_weak = media_mmu_start;
    arch_shutdown_mmu_weak = media_mmu_stop;
}
KOS_INIT_EARLY(media_early);
struct mapped_workspace { void *backing, *view; size_t bytes; mmucontext_t *context; };
static int workspace_release(struct mapped_workspace *w) {
    int result = 0;
    if(w->context) {
        if(w->view && mmu_page_unmap_ex(w->context,
                (uintptr_t)w->view >> PAGESIZE_BITS, w->bytes >> PAGESIZE_BITS)) result = -1;
        /* Destructor also retires any remaining mapping after an unmap failure.
           Sole table owner; no decoder/callback may still be active here. */
        mmu_context_destroy(w->context);
        w->context = NULL;
    }
    /* The direct A25 view must retire dirty lines before its canonical
       allocation can return to the heap. No borrower survives this point. */
    else if(w->view) {
        for(size_t off = 0; off < w->bytes; off += 16384) {
            size_t span = w->bytes - off;
            if(span > 16384) span = 16384;
            dcache_purge_range((uintptr_t)w->view + off, span);
        }
    }
    w->view = NULL;
    workspace_live = false;
    free(w->backing); w->backing = NULL;
    return result;
}
static int workspace_acquire(struct mapped_workspace *w) {
    uintptr_t va = 0x0e000000u; /* Reserved P0 range on the translated route. */
    size_t needed = kosdec_sfd_mpeg_movie_workspace_size();
    if(oix_admission != 1 || !oix_owned || !mmu_enabled() || mmu_cxt_current ||
       (*(volatile uint32_t *)0xff000000 & 255) ||
       (*(volatile uint32_t *)0xff00001c & (CCR_OIX | CCR_ORA)) != CCR_OIX ||
       needed > SIZE_MAX - (PAGESIZE - 1)) return -1;
    w->bytes = (needed + PAGESIZE - 1) & ~(size_t)(PAGESIZE - 1);
    if(w->bytes > 0x01000000u || va % kosdec_sfd_mpeg_movie_workspace_alignment()) return -1;
    w->backing = aligned_alloc(PAGESIZE, w->bytes);
    if(!w->backing) return -1;
    uintptr_t address = (uintptr_t)w->backing;
    uintptr_t pa = address & MEM_AREA_CACHE_MASK;
    if((address & ~MEM_AREA_CACHE_MASK) != MEM_AREA_P1_BASE ||
       pa < 0x0c000000u || pa >= 0x0c000000u + HW_MEMSIZE ||
       w->bytes > 0x0c000000u + HW_MEMSIZE - pa || (pa & 0x02000000u)) goto fail;
    memset(w->backing, 0, w->bytes);
    ((uint32_t *)w->backing)[0] = 0x6d706731;
    ((uint32_t *)w->backing)[1] = 0x92a135ec;
    for(size_t off = 0; off < w->bytes; off += 16384) {
        size_t span = w->bytes - off;
        if(span > 16384) span = 16384;
        dcache_purge_range(address + off, span);
    }
    if(oix_route == ADDRESS_PHYSICAL) {
        /* AMX2/4 do not use physical A25 for the SDRAM bank/row address.
           Canonical backing is no longer accessed until this view retires. */
        va = address | 0x02000000u;
        w->view = (void *)va;
    }
    else {
        w->context = mmu_context_create(0);
        if(!w->context || mmu_page_map_ex(w->context, va >> PAGESIZE_BITS,
                pa >> PAGESIZE_BITS, w->bytes >> PAGESIZE_BITS,
                MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true)) goto fail;
        int irq = irq_disable();
        mmu_use_table(w->context);
        mmu_switch_context(w->context);
        irq_restore(irq);
        w->view = (void *)va;
        /* Public snapshots only; whole-program ownership provides stability. */
        if(mmu_cxt_current != w->context || w->context->asid != 0 ||
           (*(volatile uint32_t *)0xff000000 & 255)) goto fail;
        unsigned vpn = va >> PAGESIZE_BITS;
        const mmupage_t *entry = &w->context->sub[vpn / MMU_SUB_PAGES]->page[vpn % MMU_SUB_PAGES];
        printf("KOSDEC_SFD_OIX_MAP VA=%08lx PA=%08lx pages=%lu table=%p ASID=%lu MMUCR=%08lx "
               "entry_physical=%05lx valid=%u cache=%u PTEH=%08lx PTEL=%08lx\n",
               (unsigned long)va, (unsigned long)pa, (unsigned long)(w->bytes >> PAGESIZE_BITS),
               (void *)mmu_cxt_current, (unsigned long)(*(volatile uint32_t *)0xff000000 & 255),
               (unsigned long)(*(volatile uint32_t *)0xff000010), (unsigned long)entry->physical,
               (unsigned)entry->valid, (unsigned)entry->cache,
               (unsigned long)entry->pteh, (unsigned long)entry->ptel);
    }
    workspace_live = true;
    /* Check the selected view; actual decode uses this entire span. */
    uint32_t observed0 = ((volatile uint32_t *)va)[0];
    uint32_t observed1 = ((volatile uint32_t *)va)[1];
    if(observed0 != 0x6d706731 || observed1 != 0x92a135ec) {
        printf("KOSDEC_SFD_OIX UNAVAILABLE selected-address probe failed observed=%08lx,%08lx; no fallback\n",
               (unsigned long)observed0, (unsigned long)observed1);
        goto fail;
    }
    printf("KOSDEC_SFD_OIX workspace=%lu align=%lu rounded=%lu pages=%lu CPU_VIEW=%08lx BACKING_PA=%08lx RAM=%lu\n",
           (unsigned long)needed, (unsigned long)kosdec_sfd_mpeg_movie_workspace_alignment(),
           (unsigned long)w->bytes, (unsigned long)(w->bytes >> PAGESIZE_BITS),
           (unsigned long)va, (unsigned long)pa, (unsigned long)HW_MEMSIZE);
    return 0;
fail:
    {
        int cleanup = workspace_release(w);
        printf("KOSDEC_SFD_OIX_FAIL_CLEANUP result=%d table=%p ASID=%lu live=%u CCR=%08lx\n",
               cleanup, (void *)mmu_cxt_current, (unsigned long)(*(volatile uint32_t *)0xff000000 & 255),
               (unsigned)workspace_live, (unsigned long)(*(volatile uint32_t *)0xff00001c));
    }
    return -1;
}
#endif

struct presentation {
    uint64_t epoch, first, last, worst_late, holds;
    uint64_t decode_resume, decode_us, flip_times[31];
    uint64_t convert_us, hud_us, deadline_us, vblank_us;
    unsigned show_fps10, decode_fps10;
    size_t flips, late;
    bool cancelled;
};
static bool cancel_requested(void) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *s = dev ? maple_dev_status(dev) : NULL;
    return s && (s->buttons & CONT_START);
}
static int wait_until(uint64_t deadline, struct presentation *p) {
    for(;;) {
        if(cancel_requested()) { p->cancelled = true; return -1; }
        uint64_t now = timer_us_gettime64();
        if(now >= deadline) return 0;
        uint64_t left = deadline - now;
        thd_sleep(left > 2000 ? (int)((left - 1000) / 1000) : 1);
    }
}
static int present(const uint8_t *planes, size_t index, void *opaque) {
    struct presentation *p = opaque;
    uint64_t entered = timer_us_gettime64();
    p->decode_us += entered - p->decode_resume;
    if(index != p->flips) {
        printf("KOSDEC_SFD_FAIL order index=%lu expected=%lu\n", (unsigned long)index, (unsigned long)p->flips);
        return 1;
    }
    if(cancel_requested()) { p->cancelled = true; return 1; }
    p->decode_fps10 = p->decode_us ? (unsigned)((index + 1) * UINT64_C(10000000) / p->decode_us) : 0;
    /* Initialization leaves vram_s at the next draw slot. Each flip advances it.
       Borrowed planes are used synchronously; no DMA or retained callback data. */
    uint64_t phase = timer_us_gettime64();
    kosdec_sfd_rgb565(planes, vram_s);
    uint64_t stamp = timer_us_gettime64();
    p->convert_us += stamp - phase; phase = stamp;
    kosdec_sfd_overlay(vram_s, (unsigned)index + 1, p->show_fps10, p->decode_fps10);
    stamp = timer_us_gettime64();
    p->hud_us += stamp - phase;
    if(!index) p->epoch = timer_us_gettime64();
    uint64_t deadline = p->epoch + index * UINT64_C(1000000) / 30 + p->holds;
    phase = timer_us_gettime64();
    if(wait_until(deadline, p)) return 1;
    stamp = timer_us_gettime64();
    p->deadline_us += stamp - phase; phase = stamp;
    /* At least one refresh between selections, even if decoding is late. */
    vid_waitvbl();
    vid_flip(-1);
    p->last = timer_us_gettime64();
    p->vblank_us += p->last - phase;
    uint64_t late = p->last > deadline ? p->last - deadline : 0;
    if(late > 1000) ++p->late;
    if(late > p->worst_late) p->worst_late = late;
    if(!index) p->first = p->last;
    size_t intervals = index < 30 ? index : 30;
    if(intervals) {
        uint64_t span = p->last - p->flip_times[(index - intervals) % 31];
        p->show_fps10 = span ? (unsigned)(intervals * UINT64_C(10000000) / span) : 0;
    }
    p->flip_times[index % 31] = p->last;
    ++p->flips;
    if(index % 30 == 0 || index == 179 || index == 359)
        printf("KOSDEC_SFD_PRESENT index=%lu decoded=%lu flips=%lu show_fps10=%u decode_fps10=%u elapsed_us=%llu late_us=%llu VIDEO_ONLY\n",
               (unsigned long)index, (unsigned long)index + 1, (unsigned long)p->flips,
               p->show_fps10, p->decode_fps10,
               (unsigned long long)(p->last - p->first), (unsigned long long)late);
    if(CAPTURE_HOLD_MS && (index == 0 || index == 179)) {
        uint64_t start = timer_us_gettime64();
        if(wait_until(start + UINT64_C(1000) * CAPTURE_HOLD_MS, p)) return 1;
        p->holds += timer_us_gettime64() - start;
    }
    p->decode_resume = timer_us_gettime64();
    return 0;
}
int main(void) {
    /* Serial only: framebuffer console drawing would violate display ownership. */
    dbgio_dev_select("scif");
#if MEDIA_OIX
    printf("KOSDEC_SFD_ADDRESS MCR=%08lx AMX=%lu EXT=%lu SZ=%lu route=%s BCR1=%08lx RAM=%lu\n",
           (unsigned long)startup_mcr, (unsigned long)((startup_mcr >> 3) & 7),
           (unsigned long)((startup_mcr >> 6) & 1), (unsigned long)((startup_mcr >> 7) & 3),
           oix_route == ADDRESS_PHYSICAL ? "untranslated-A25" :
           oix_route == ADDRESS_TRANSLATED ? "translated-A25" : "unsupported",
           (unsigned long)startup_bcr1, (unsigned long)HW_MEMSIZE);
    if(oix_admission != 1 || !address_profile_supported(startup_bcr1, startup_mcr, HW_MEMSIZE)) {
        printf("KOSDEC_SFD_OIX REFUSED memory profile or startup ownership\n"); return 1;
    }
#else
    printf("KOSDEC_SFD_COMPARISON OIX_OFF: not the required mapped product path\n");
#endif
    printf("KOSDEC_SFD_VIDEO start input=%lu capture_hold_ms=%u ccr=%08lx OIX=%u\n",
           (unsigned long)sizeof(movie_sfd), (unsigned)CAPTURE_HOLD_MS,
           (unsigned long)(*(volatile uint32_t *)0xff00001c),
           !!(*(volatile uint32_t *)0xff00001c & CCR_OIX));
    struct kosdec_sfd_info info;
    if(kosdec_sfd_video_extract(movie_sfd, sizeof(movie_sfd), NULL, 0, &info) != KOSDEC_SFD_OK) {
        printf("KOSDEC_SFD_FAIL input\n"); return 1;
    }
    size_t video_bytes = info.video_bytes;
    uint8_t *video = malloc(video_bytes);
    if(!video) { printf("KOSDEC_SFD_FAIL allocation\n"); return 1; }
    if(kosdec_sfd_video_extract(movie_sfd, sizeof(movie_sfd), video, video_bytes, &info) != KOSDEC_SFD_OK) {
        printf("KOSDEC_SFD_FAIL extraction\n"); free(video); return 1;
    }
    printf("KOSDEC_SFD_DEMUX bytes=%lu packets=%lu audio_skipped=%lu VIDEO_ONLY\n",
           (unsigned long)video_bytes, (unsigned long)info.video_packets,
           (unsigned long)info.audio_packets);
    if(vid_check_cable() == CT_NONE ||
       vid_init_checked(DM_320x240 | DM_MULTIBUFFER, PM_RGB565)) {
        printf("KOSDEC_SFD_FAIL mode\n"); free(video); return 1;
    }
    vid_framebuffer_info_t draw, display;
    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DRAW, &draw) ||
       vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED, &display) ||
       draw.width != 320 || draw.height != 240 || draw.pixel_mode != PM_RGB565 ||
       draw.stride_bytes != 640 || draw.visible_bytes != 153600 ||
       draw.capacity_bytes < draw.visible_bytes || draw.address != vram_s ||
       draw.address == display.address || vid_mode->fb_count < 2) {
        printf("KOSDEC_SFD_FAIL framebuffer\n"); free(video); return 1;
    }
    printf("KOSDEC_SFD_DISPLAY slots=%u bytes_per_slot=%lu draw=%ld display=%ld\n",
           (unsigned)vid_mode->fb_count, (unsigned long)draw.capacity_bytes,
           (long)draw.index, (long)display.index);
    struct presentation p = {0};
    struct kosdec_sfd_mpeg_stats stats;
#if MEDIA_OIX
    struct mapped_workspace workspace = {0};
    if(workspace_acquire(&workspace)) {
        printf("KOSDEC_SFD_FAIL OIX workspace\n"); free(video); return 1;
    }
#endif
    p.decode_resume = timer_us_gettime64();
#if MEDIA_OIX
    enum kosdec_sfd_mpeg_status result = kosdec_sfd_mpeg_decode_movie_workspace(video, video_bytes, 360,
        workspace.view, workspace.bytes, NULL, present, &p, &stats);
    int retired = workspace_release(&workspace);
    printf("KOSDEC_SFD_OIX retired=%d table=%p ASID=%lu CCR=%08lx (restore at late shutdown)\n",
           retired, (void *)mmu_cxt_current, (unsigned long)(*(volatile uint32_t *)0xff000000 & 255),
           (unsigned long)(*(volatile uint32_t *)0xff00001c));
    if(retired) result = KOSDEC_SFD_MPEG_INVALID;
#else
    enum kosdec_sfd_mpeg_status result = kosdec_sfd_mpeg_decode_movie(video, video_bytes, 360, NULL, present, &p, &stats);
#endif
    /* Keep the selected final/last good image unchanged through another refresh. */
    if(p.flips) vid_waitvbl();
    free(video);
    printf("KOSDEC_SFD_PHASE decode_us=%llu convert_and_vram_us=%llu hud_us=%llu deadline_wait_us=%llu vblank_flip_us=%llu\n",
           (unsigned long long)p.decode_us, (unsigned long long)p.convert_us,
           (unsigned long long)p.hud_us, (unsigned long long)p.deadline_us,
           (unsigned long long)p.vblank_us);
    printf("KOSDEC_SFD_COMPLETE status=%d decoded=%lu flips=%lu blocks=%lu work=%lu arena=%lu "
           "elapsed_us=%llu capture_holds_us=%llu late=%lu worst_late_us=%llu show_fps10=%u decode_fps10=%u result=%s VIDEO_ONLY\n",
           result, (unsigned long)stats.frames, (unsigned long)p.flips,
           (unsigned long)stats.blocks, (unsigned long)stats.work, (unsigned long)stats.allocated,
           (unsigned long long)(p.last - p.first), (unsigned long long)p.holds,
           (unsigned long)p.late, (unsigned long long)p.worst_late,
           p.show_fps10, p.decode_fps10,
           result == KOSDEC_SFD_MPEG_OK && p.flips == 360 ? "PASS" : p.cancelled ? "CANCEL" : "FAIL");
    if(!p.cancelled) {
        printf("KOSDEC_SFD_HOLD last image retained; press START to exit\n");
        while(!cancel_requested()) thd_sleep(20);
    }
    /* Normal KOS shutdown owns restoring loader video. */
    return result == KOSDEC_SFD_MPEG_OK || p.cancelled ? 0 : 1;
}
