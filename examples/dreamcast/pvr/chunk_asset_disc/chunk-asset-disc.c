/* KallistiOS ##version##

   Direct-disc compact-model asset benchmark.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dc/fs_iso9660.h>
#include <dc/gaps.h>
#include <dc/g2bus.h>
#include <dc/gdrom_direct.h>
#include <dc/pvr_chunk_asset_io.h>
#include <kos/init.h>
#include <kos/pvr_chunk_asset_lz4.h>
#include <kos/timer.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#ifndef CHUNK_ASSET_FAD
#define CHUNK_ASSET_FAD 0u
#endif
#ifndef CHUNK_ASSET_BYTES
#define CHUNK_ASSET_BYTES 0u
#endif
#ifndef CHUNK_ASSET_PATH
#define CHUNK_ASSET_PATH "/cd/chunk-asset-model.pcm"
#endif
#ifndef CHUNK_ASSET_SECTOR_TYPE
#define CHUNK_ASSET_SECTOR_TYPE GDROM_DIRECT_SECTOR_MODE1
#endif

#define READ_TIMEOUT_MS 12000u

static int model_info_equal(const pvr_chunk_model_info_t *left,
                            const pvr_chunk_model_info_t *right) {
    return left->vertex_records == right->vertex_records
        && left->vertex_entries == right->vertex_entries
        && left->shape_records == right->shape_records
        && left->polygon_records == right->polygon_records
        && left->material_records == right->material_records
        && left->strip_records == right->strip_records
        && left->strips == right->strips
        && left->triangles == right->triangles
        && left->index_references == right->index_references
        && left->maximum_strip_vertices == right->maximum_strip_vertices
        && left->maximum_vertex_index == right->maximum_vertex_index;
}

static int decode_asset(const char *label, const pvr_chunk_asset_view_t *asset,
                        pvr_chunk_model_info_t *info, uint64_t *milliseconds) {
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    void *workspace = NULL;
    size_t allocation = 0;
    uint64_t started;
    int saved_errno;

    if(asset->vertex.codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME
            && asset->polygon.codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME) {
        errno = ENOTSUP;
        return -1;
    }
    if(pvr_chunk_asset_workspace_query(asset, &requirements) < 0)
        return -1;
    if(requirements.bytes) {
        if(requirements.bytes > SIZE_MAX - requirements.alignment + 1u) {
            errno = EOVERFLOW;
            return -1;
        }
        allocation = (requirements.bytes + requirements.alignment - 1u)
            & ~(requirements.alignment - 1u);
        workspace = aligned_alloc(requirements.alignment, allocation);
        if(!workspace)
            return -1;
    }

    started = timer_ms_gettime64();
    if(pvr_chunk_asset_load(asset, pvr_chunk_asset_lz4_decode, NULL,
                            workspace, allocation, &model) < 0) {
        saved_errno = errno;
        free(workspace);
        errno = saved_errno;
        return -1;
    }
    *milliseconds = timer_ms_gettime64() - started;
    *info = model.info;
    printf("%s decode: %llu ms, vertices=%lu triangles=%lu\n", label,
           (unsigned long long)*milliseconds,
           (unsigned long)info->vertex_entries,
           (unsigned long)info->triangles);
    free(workspace);
    return 0;
}

static void print_transport(
    const char *label, const pvr_chunk_asset_direct_result_t *result) {
    printf("%s transport: chunks=%u gd=%llu ms g2=%llu ms total=%llu ms\n",
           label, (unsigned)result->chunks,
           (unsigned long long)result->gdrom_milliseconds,
           (unsigned long long)result->g2_milliseconds,
           (unsigned long long)result->total_milliseconds);
}

static int locate_asset(uint32_t *fad, size_t *asset_bytes) {
    iso9660_file_info_t file_info;

    if(CHUNK_ASSET_FAD && CHUNK_ASSET_BYTES) {
        *fad = CHUNK_ASSET_FAD;
        *asset_bytes = CHUNK_ASSET_BYTES;
        return 0;
    }
    if(fs_iso9660_get_path_info(CHUNK_ASSET_PATH, &file_info) < 0)
        return -1;
    *fad = file_info.extent_fad;
    *asset_bytes = file_info.size;
    return 0;
}

int main(int argc, char **argv) {
    pvr_chunk_asset_direct_config_t config;
    pvr_chunk_asset_direct_result_t ram_result;
    pvr_chunk_asset_direct_result_t gaps_result;
    pvr_chunk_asset_view_t ram_view;
    pvr_chunk_asset_view_t gaps_view;
    pvr_chunk_model_info_t ram_info;
    pvr_chunk_model_info_t gaps_info;
    gaps_sram_lease_t lease = GAPS_SRAM_LEASE_INVALID;
    uint8_t *ram_asset = NULL;
    uint8_t *gaps_asset = NULL;
    size_t asset_bytes;
    size_t physical_bytes;
    size_t staging_bytes;
    uint32_t fad;
    uint64_t ram_decode_ms;
    uint64_t gaps_decode_ms;
    int gaps_initialized = 0;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;
    puts("Direct-disc compact-model asset benchmark");
    if(locate_asset(&fad, &asset_bytes) < 0) {
        printf("CHUNK-ASSET-DISC: SKIP %s is unavailable\n",
               CHUNK_ASSET_PATH);
        return EXIT_SUCCESS;
    }
    if(!asset_bytes) {
        errno = EINVAL;
        perror("asset size");
        return EXIT_FAILURE;
    }
    if(asset_bytes > SIZE_MAX - (GDROM_DIRECT_SECTOR_SIZE - 1u)) {
        errno = EOVERFLOW;
        perror("asset size");
        return EXIT_FAILURE;
    }
    physical_bytes = (asset_bytes + GDROM_DIRECT_SECTOR_SIZE - 1u)
        & ~(GDROM_DIRECT_SECTOR_SIZE - 1u);
    ram_asset = aligned_alloc(32, physical_bytes);
    gaps_asset = aligned_alloc(32, physical_bytes);
    if(!ram_asset || !gaps_asset) {
        perror("asset storage");
        goto out;
    }

    config = (pvr_chunk_asset_direct_config_t) {
        .fad = fad,
        .asset_bytes = asset_bytes,
        .sector_type = CHUNK_ASSET_SECTOR_TYPE,
        .timeout = READ_TIMEOUT_MS,
        .path = PVR_CHUNK_ASSET_DIRECT_TO_RAM,
        .gaps_lease = GAPS_SRAM_LEASE_INVALID,
        .gaps_offset = 0,
        .g2_channel = G2_DMA_CHAN_CH2
    };
    if(pvr_chunk_asset_read_direct(&config, ram_asset, physical_bytes,
                                   &ram_view, &ram_result) < 0
            || decode_asset("GD-ROM -> RAM", &ram_view, &ram_info,
                            &ram_decode_ms) < 0) {
        perror("RAM asset pipeline");
        goto out;
    }
    print_transport("GD-ROM -> RAM", &ram_result);

    if(!gaps_probe()) {
        puts("CHUNK-ASSET-DISC: PASS RAM; GAPS bridge unavailable");
        result = EXIT_SUCCESS;
        goto out;
    }
    if(gaps_init() < 0) {
        perror("GAPS initialization");
        goto out;
    }
    gaps_initialized = 1;
    staging_bytes = physical_bytes < GAPS_SRAM_SIZE
        ? physical_bytes : GAPS_SRAM_SIZE;
    if(gaps_sram_alloc(staging_bytes, GDROM_DIRECT_SECTOR_SIZE,
                       &lease) < 0) {
        if(errno == ENOMEM) {
            puts("CHUNK-ASSET-DISC: PASS RAM; GAPS SRAM already owned");
            result = EXIT_SUCCESS;
        }
        else
            perror("GAPS SRAM allocation");
        goto out;
    }

    config.path = PVR_CHUNK_ASSET_DIRECT_VIA_GAPS;
    config.gaps_lease = lease;
    if(pvr_chunk_asset_read_direct(&config, gaps_asset, physical_bytes,
                                   &gaps_view, &gaps_result) < 0
            || decode_asset("GD-ROM -> GAPS -> G2 -> RAM", &gaps_view,
                            &gaps_info, &gaps_decode_ms) < 0) {
        perror("GAPS asset pipeline");
        goto out;
    }
    if(memcmp(ram_asset, gaps_asset, physical_bytes)
            || !model_info_equal(&ram_info, &gaps_info)) {
        errno = EILSEQ;
        perror("pipeline comparison");
        goto out;
    }
    print_transport("GD-ROM -> GAPS -> G2 -> RAM", &gaps_result);
    puts("CHUNK-ASSET-DISC: PASS both pipelines");
    result = EXIT_SUCCESS;

out:
    if(lease != GAPS_SRAM_LEASE_INVALID)
        (void)gaps_sram_free(lease);
    if(gaps_initialized)
        (void)gaps_shutdown();
    free(gaps_asset);
    free(ram_asset);
    return result;
}
