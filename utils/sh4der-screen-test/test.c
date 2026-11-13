#include <sh4der_kos_screen.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#ifndef __DREAMCAST__
static unsigned compiled;
void pvr_poly_cxt_col(pvr_poly_cxt_t *c, pvr_list_t list) {
    memset(c, 0, sizeof(*c)); c->list_type = list;
}
void pvr_poly_compile(pvr_poly_hdr_t *h, const pvr_poly_cxt_t *c) {
    assert(c->list_type == PVR_LIST_TR_POLY);
    assert(c->gen.culling == PVR_CULLING_NONE && c->gen.fog_type == PVR_FOG_DISABLE);
    assert(c->depth.write == PVR_DEPTHWRITE_DISABLE && c->depth.comparison == PVR_DEPTHCMP_ALWAYS);
    memset(h, 0, sizeof(*h));
    h->words[0] = c->blend.src; h->words[1] = c->blend.dst;
    ++compiled;
}
#endif

int main(void) {
    alignas(32) sh4der_kos_screen_packet_t packets[6], saved[6];
    sh4der_look_grade_t grade = {.gain = {1, 1, 1}, .contrast = 1, .exposure = 1};
    const float rect[4] = {10, 20, 320, 240};
    memset(packets, 0x5a, sizeof(packets)); memcpy(saved, packets, sizeof(saved));
    assert(sh4der_kos_grade_packets(&grade, rect, 10, 1, packets, 6) == 0);
    assert(!memcmp(saved, packets, sizeof(saved)));
    /* Three subtract layers, then multiply, boost and add: all six kinds/order. */
    grade.gain[0] = 0.5f; grade.gain[1] = 1.5f;
    grade.lift[0] = -0.2f; grade.lift[2] = 0.2f;
    assert(sh4der_kos_grade_packets(&grade, rect, 10, 1, packets, 5) == -1);
    assert(errno == ENOSPC && !memcmp(saved, packets, sizeof(saved)));
    assert(sh4der_kos_grade_packets(&grade, rect, 10, 1, packets, 6) == 6);
    for(unsigned i = 0; i < 6; ++i)
        for(unsigned j = 0; j < 4; ++j) {
            pvr_vertex_t *v = &packets[i].vertices[j];
            assert(v->x == 10 + ((j & 1) ? 320 : 0));
            assert(v->y == 20 + ((j & 2) ? 240 : 0));
            assert(v->z == 10 + i && v->oargb == 0);
            assert(v->flags == (j == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
        }
#ifndef __DREAMCAST__
    const unsigned src[6] = {PVR_BLEND_INVDESTCOLOR, PVR_BLEND_ONE, PVR_BLEND_INVDESTCOLOR,
        PVR_BLEND_ZERO, PVR_BLEND_DESTCOLOR, PVR_BLEND_ONE};
    const unsigned dst[6] = {PVR_BLEND_ZERO, PVR_BLEND_ONE, PVR_BLEND_ZERO,
        PVR_BLEND_DESTCOLOR, PVR_BLEND_ONE, PVR_BLEND_ONE};
    assert(compiled == 6);
    for(unsigned i = 0; i < 6; ++i) {
        assert(packets[i].header.words[0] == src[i]);
        assert(packets[i].header.words[1] == dst[i]);
    }
#endif
    memcpy(saved, packets, sizeof(saved));
    assert(sh4der_kos_grade_packets(&grade, rect, 1e30f, 1, packets, 6) == -1);
    assert(errno == ERANGE && !memcmp(saved, packets, sizeof(saved)));
    grade.exposure = NAN;
    assert(sh4der_kos_grade_packets(&grade, rect, 10, 1, packets, 6) == -1);
    assert(errno == EDOM && !memcmp(saved, packets, sizeof(saved)));
    puts("PASS: grade layer order, contexts, rectangles, depth, capacity and failure preservation");
    return 0;
}
