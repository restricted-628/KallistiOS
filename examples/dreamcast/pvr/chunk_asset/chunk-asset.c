/* KallistiOS ##version##

   Compact-model asset container example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/pvr_chunk_asset_lz4.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern const unsigned char chunk_asset_model_data[];
extern const int chunk_asset_model_size;

int main(void) {
    pvr_chunk_asset_view_t asset;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    void *workspace = NULL;
    size_t allocation = 0;
    int result = 1;

    if(chunk_asset_model_size <= 0 ||
       pvr_chunk_asset_open(chunk_asset_model_data,
                            (size_t)chunk_asset_model_size, &asset) < 0 ||
       pvr_chunk_asset_workspace_query(&asset, &requirements) < 0) {
        printf("KOSPVRASSET open=0 errno=%d\n", errno);
        return 1;
    }

    if(requirements.bytes) {
        allocation = (requirements.bytes + requirements.alignment - 1u) &
                     ~(requirements.alignment - 1u);
        workspace = aligned_alloc(requirements.alignment, allocation);
        if(!workspace) {
            printf("KOSPVRASSET workspace=0 errno=%d\n", errno);
            return 1;
        }
    }

    if(pvr_chunk_asset_load(&asset, pvr_chunk_asset_lz4_decode, NULL,
                            workspace, allocation, &model) == 0 &&
       model.info.vertex_entries == 3 && model.info.triangles == 1) {
        printf("KOSPVRASSET loaded=1 vertices=%lu triangles=%lu "
               "workspace=%lu\n",
               (unsigned long)model.info.vertex_entries,
               (unsigned long)model.info.triangles,
               (unsigned long)requirements.bytes);
        result = 0;
    }
    else {
        printf("KOSPVRASSET loaded=0 errno=%d\n", errno);
    }

    free(workspace);
    return result;
}
