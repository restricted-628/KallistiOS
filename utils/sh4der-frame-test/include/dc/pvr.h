#ifndef SH4DER_FRAME_TEST_PVR_H
#define SH4DER_FRAME_TEST_PVR_H
#include "../../../pvr-reservation-test/include/dc/pvr.h"
typedef uint64_t pvr_render_id_t;
typedef enum pvr_render_stage {
    PVR_RENDER_STAGE_QUEUED, PVR_RENDER_STAGE_REGISTERED,
    PVR_RENDER_STAGE_RENDERING, PVR_RENDER_STAGE_COMPLETE, PVR_RENDER_STAGE_DISPLAYED
} pvr_render_stage_t;
typedef struct pvr_render_ticket {
    pvr_render_id_t id;
    pvr_ptr_t target;
    uint32_t width, height, stride, to_texture;
} pvr_render_ticket_t;
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *, pvr_render_stage_t *);
#endif
