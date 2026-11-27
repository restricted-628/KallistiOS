/* Explicit retail GD-to-AICA test using an allocated, unplayed sound range. */
#include <kos.h>
#include <dc/cdrom.h>
#include <dc/sound/sound.h>
#include <dc/spu.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdrom_direct_internal.h"
#include "g1_bus.h"

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_EXPORT);

#define GUARD_BYTES 32u
#define TEST_SECTORS (GDROM_DIRECT_DMA_MAX_SECTORS + 1u)
#define TEST_BYTES (TEST_SECTORS * GDROM_DIRECT_SECTOR_SIZE)
#define AREA_BYTES (TEST_BYTES + 2 * GUARD_BYTES)

_Alignas(32) static uint8_t expected[TEST_BYTES];
_Alignas(32) static uint8_t sound[AREA_BYTES];

static bool guards_intact(void) {
    for(unsigned i = 0; i < GUARD_BYTES; ++i) {
        if(sound[i] != 0xa5 || sound[GUARD_BYTES + TEST_BYTES + i] != 0xa5)
            return false;
    }
    return true;
}

int main(void) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_result_t result = {0};
    gdrom_direct_sector_type_t format;
    cd_toc_t toc;
    uint32_t offset;
    uint32_t fad;
    int status, error;

    const export_sym_t *sym = export_lookup("gdrom_direct_load_aica");
    if(!sym || sym->ptr != (uintptr_t)gdrom_direct_load_aica) {
        puts("GD-AICA FAIL: public constructor unavailable in subarch exports");
        return EXIT_FAILURE;
    }
    if(hardware_sys_mode(NULL) != HW_TYPE_RETAIL) {
        puts("GD-AICA SKIP: retail Dreamcast required");
        return EXIT_FAILURE;
    }
    if(gdrom_direct_probe(&probe, 4000) < 0
            || gdrom_direct_read_toc(&toc, false, 4000, NULL) < 0) {
        printf("GD-AICA FAIL: disc probe/TOC errno=%d\n", errno);
        return EXIT_FAILURE;
    }
    fad = cdrom_locate_data_track(&toc);
    if(!fad) {
        puts("GD-AICA FAIL: no low-density data track");
        return EXIT_FAILURE;
    }
    format = probe.status.disc_type == CD_CDROM_XA
             || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1 : GDROM_DIRECT_SECTOR_MODE1;

    if(gdrom_direct_read_sectors_dma(expected, fad, TEST_SECTORS,
                                     format, 4000, NULL) < 0) {
        printf("GD-AICA FAIL: reference disc read errno=%d\n", errno);
        return EXIT_FAILURE;
    }
    if(snd_init() < 0) {
        puts("GD-AICA FAIL: sound init");
        return EXIT_FAILURE;
    }
    offset = snd_mem_malloc(AREA_BYTES);
    if(!offset) {
        puts("GD-AICA FAIL: sound allocation");
        snd_shutdown();
        return EXIT_FAILURE;
    }

    memset(sound, 0xa5, sizeof(sound));
    spu_memload(offset, sound, sizeof(sound));
    g2_fifo_wait();

    void *wave = (void *)(SPU_RAM_UNCACHED_BASE + offset + GUARD_BYTES);
    bool ordinary_refused = gdrom_direct_read_sectors_dma(
        wave, fad, 1, format, 500, NULL) < 0 && errno == EFAULT;
    bool crossing_refused = gdrom_direct_read_sectors_dma_aica(
        SPU_RAM_SIZE_2MB - TEST_BYTES + GDROM_DIRECT_SECTOR_SIZE,
        fad, TEST_SECTORS, format, 500, &result) < 0
        && errno == EFAULT && result.transferred == 0;
    bool firmware_refused = gdrom_direct_read_sectors_dma_aica(
        0, fad, 1, format, 500, &result) < 0
        && errno == EFAULT && result.transferred == 0;
    status = gdrom_direct_read_sectors_dma_aica(
        offset + GUARD_BYTES, fad, TEST_SECTORS, format, 500, &result);
    error = status < 0 ? errno : 0;
    spu_memread(sound, offset, sizeof(sound));
    bool match = memcmp(expected, sound + GUARD_BYTES, sizeof(expected)) == 0;
    bool guards = guards_intact();
    bool canonical_ok = status == 0 && result.transferred == TEST_BYTES
        && result.dma_transferred == GDROM_DIRECT_SECTOR_SIZE && match && guards;
    memset(sound, 0xa5, sizeof(sound));
    spu_memload(offset, sound, sizeof(sound));
    g2_fifo_wait();
    status = gdrom_direct_read_sectors_dma_aica(
        SPU_RAM_UNCACHED_BASE + 0x02000000u + offset + GUARD_BYTES,
        fad, TEST_SECTORS, format, 500, &result);
    if(status < 0)
        error = errno;
    spu_memread(sound, offset, sizeof(sound));
    bool image_ok = status == 0 && result.transferred == TEST_BYTES
        && result.dma_transferred == GDROM_DIRECT_SECTOR_SIZE
        && memcmp(expected, sound + GUARD_BYTES, sizeof(expected)) == 0
        && guards_intact();
    /* Create an exact-sized hole bracketed by owned allocations. This
       tests the public constructor without telling it what address to use. */
    uint32_t before = snd_mem_malloc(GUARD_BYTES);
    uint32_t hole = snd_mem_malloc(TEST_BYTES);
    uint32_t after = snd_mem_malloc(GUARD_BYTES);
    bool adjacent = before && hole && after && before + GUARD_BYTES == hole
                 && after == hole + TEST_BYTES;
    uint32_t asset = UINT32_MAX;
    uint32_t final_gdstar = 0;
    bool public_ok = false;
    if(adjacent) {
        memset(sound, 0xbd, sizeof(sound));
        spu_memload(before, sound, GUARD_BYTES);
        spu_memload(after, sound, GUARD_BYTES);
        g2_fifo_wait();
    }
    if(hole)
        snd_mem_free(hole);
    if(adjacent && gdrom_direct_load_aica(&asset, fad, 0, format, 500, NULL) < 0
            && errno == EINVAL && asset == UINT32_MAX
            && gdrom_direct_load_aica(&asset, fad, TEST_SECTORS,
                                      format, 500, &result) == 0) {
        /* Closed fixture: no other disc/ATA client. Last command is one sector. */
        final_gdstar = *(volatile uint32_t *)G1_ATA_DMA_ADDRESS;
        spu_memread(sound, before, sizeof(sound));
        public_ok = asset == hole && result.transferred == TEST_BYTES
            && result.dma_transferred == GDROM_DIRECT_SECTOR_SIZE
            && final_gdstar == SPU_RAM_BASE + asset + TEST_BYTES - GDROM_DIRECT_SECTOR_SIZE
            && result.dma_current_address == SPU_RAM_BASE + asset + TEST_BYTES
            && !memcmp(expected, sound + GUARD_BYTES, TEST_BYTES);
        for(size_t i = 0; i < GUARD_BYTES; ++i)
            public_ok &= sound[i] == 0xbd && sound[GUARD_BYTES + TEST_BYTES + i] == 0xbd;
        snd_mem_free(asset);
    }
    if(before)
        snd_mem_free(before);
    if(after)
        snd_mem_free(after);

    bool pass = ordinary_refused && crossing_refused && firmware_refused
        && canonical_ok && image_ok && public_ok;

    snd_mem_free(offset);
    snd_shutdown();
    printf("GD-AICA %s: errno=%d event=%04lx bytes=%lu canonical=%d image=%d owned=%d gdstar=%08lx end=%08lx refusal=%d/%d/%d\n",
           pass ? "PASS" : "FAIL", error, (unsigned long)result.dma_event,
           (unsigned long)result.transferred, canonical_ok, image_ok, public_ok,
           (unsigned long)final_gdstar, (unsigned long)result.dma_current_address,
           ordinary_refused, crossing_refused, firmware_refused);
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
