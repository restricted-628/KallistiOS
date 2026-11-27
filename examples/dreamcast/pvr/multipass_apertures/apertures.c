/* KallistiOS: five windows, seven tile registrations, one framebuffer render.
   All geometry and probes are generated here. */
#include <kos.h>
#include <errno.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

#define CHECK(x) do { if(!(x)) { \
    printf("APERTURE_FAIL mode=%u frame=%u pass=%u line=%d errno=%d: %s\n", \
           mode, frame, pass, __LINE__, errno, #x); exit(1); } } while(0)

enum { W = 640, H = 480, NPASS = 7, NFRAME = 2 };
enum { BUFFERED, MIXED_SQ, LATE_FLUSH, ASYNC_FLUSH };
static unsigned mode, frame, pass;
static alignas(32) uint8_t staging[NPASS][8192];
static uint16_t reference[NFRAME][H][W];
static volatile unsigned registered, rendered, displayed;
static volatile uint32_t reg_start, reg_cursor, reg_limit;
static size_t app_packet_bytes;
static const int window_for_pass[NPASS] = {-1, 0, 1, -1, 2, 3, 4};
static const uint32_t colors[] = {
    0xffff0000, 0xff00ff00, 0xffffff00, 0xff00ffff, 0xffff00ff
};
static const uint16_t rgb565[] = {0xf800, 0x07e0, 0xffe0, 0x07ff, 0xf81f};

static pvr_list_t list_at(unsigned p) {
    return p == 1 ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
}

static void event(pvr_event_t e, uint32_t detail, void *unused) {
    (void)detail;
    (void)unused;
    if(e == PVR_EVENT_REGISTRATION_COMPLETE) {
        reg_start = PVR_GET(PVR_TA_VERTBUF_START);
        reg_cursor = PVR_GET(PVR_TA_VERTBUF_POS);
        reg_limit = PVR_GET(PVR_TA_VERTBUF_END);
        ++registered;
    }
    if(e == PVR_EVENT_RENDER_COMPLETE) ++rendered;
    if(e == PVR_EVENT_DISPLAY) ++displayed;
}

typedef struct { int x, y; } pixel_xy_t;
static pixel_xy_t next_pane[4];

static void prepare_rect(pixel_xy_t p[4], int x0, int y0, int x1, int y1) {
    p[0]=(pixel_xy_t){x0,y0}; p[1]=(pixel_xy_t){x1,y0};
    p[2]=(pixel_xy_t){x0,y1}; p[3]=(pixel_xy_t){x1,y1};
}

static void polygon(const pixel_xy_t *p, unsigned n, float z, uint32_t color,
                    pvr_clip_mode_t clip) {
    pvr_poly_cxt_t context;
    alignas(32) pvr_poly_hdr_t header;
    alignas(32) pvr_vertex_t v[4] = {0};
    pvr_poly_cxt_col(&context, list_at(pass));
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.clip_mode = clip;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    context.depth.write = PVR_DEPTHWRITE_ENABLE;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    pvr_poly_compile(&header, &context);
    CHECK(pvr_prim(&header, sizeof(header)) == 0);
    for(unsigned i = 0; i < n; ++i) {
        v[i].flags = i + 1 == n ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        v[i].x = p[i].x; v[i].y = p[i].y; v[i].z = z; v[i].argb = color;
    }
    CHECK(pvr_prim(v, n * sizeof(*v)) == 0);
    app_packet_bytes += sizeof(header) + n * sizeof(*v);
}

static void rect(int x0, int y0, int x1, int y1, float z, uint32_t color,
                 pvr_clip_mode_t clip) {
    pixel_xy_t p[4];
    prepare_rect(p,x0,y0,x1,y1);
    polygon(p, 4, z, color, clip);
}

static void wall(int x) {
    /* Four near triangles leave one diamond open for a farther rectangle. */
    const pixel_xy_t corners[4][3] = {
        {{x-48,160},{x,160},{x-48,208}}, {{x+48,160},{x,160},{x+48,208}},
        {{x-48,256},{x,256},{x-48,208}}, {{x+48,256},{x,256},{x+48,208}}
    };
    for(unsigned i = 0; i < 4; ++i)
        polygon(corners[i], 3, 4, 0xffffffff, PVR_USERCLIP_DISABLE);
}

static void draw_pass(void) {
    int window = window_for_pass[pass];
    if(pass == 3) return; /* Enabled list, no application packets. */
    CHECK(pvr_list_begin(list_at(pass)) == 0);
    if(pass == 0) {
        for(int i = 0; i < 5; ++i) wall(64 + 128*i);
    }
    else {
        int x = 64 + 128*window;
        if(pass==2)
            polygon(next_pane,4,2,colors[(window+frame)%5],PVR_USERCLIP_DISABLE);
        else
            rect(x-48,160,x+48,256,2,colors[(window+frame)%5],PVR_USERCLIP_DISABLE);
        if(pass == NPASS-1) {
            /* Reissue identical bounds. The two lists of pixels are complements. */
            const pvr_user_clip_t bounds = { 8, 10, 11, 11 };
            CHECK(pvr_user_clip_submit(list_at(pass), &bounds) == 0);
            app_packet_bytes += 32;
            rect(192,320,448,384,2,0xffff0000,PVR_USERCLIP_INSIDE);
            CHECK(pvr_user_clip_submit(list_at(pass), &bounds) == 0);
            app_packet_bytes += 32;
            rect(192,320,448,384,3,0xff00ff00,PVR_USERCLIP_OUTSIDE);
        }
    }
    CHECK(pvr_list_finish() == 0);
    if(pass == 1) {
        pvr_list_flush_ticket_t ticket;
        if(mode == LATE_FLUSH)
            CHECK(pvr_list_flush(list_at(pass)) == 0);
        if(mode == ASYNC_FLUSH) {
            CHECK(pvr_list_flush_async(list_at(pass), &ticket) == 0 && ticket.id);
            CHECK(pvr_scene_next_pass() == -1 && errno == EBUSY);
            CHECK(pvr_scene_finish() == -1 && errno == EBUSY);
        }
        /* CPU work independent of the borrowed list, used by the next pass. */
        const int x = 64 + 128*window_for_pass[2];
        prepare_rect(next_pane,x-48,160,x+48,256);
        if(mode == ASYNC_FLUSH) {
            CHECK(pvr_list_flush_wait(&ticket) == 0);
            CHECK(pvr_list_flush_wait(&ticket) == -1 && errno == ENOENT);
        }
    }
}

static uint32_t check_regions(void) {
    const unsigned cols=W/32, rows=H/32, tiles=cols*rows;
    uint32_t addr=PVR_GET(PVR_ISP_TILEMAT_ADDR);
    CHECK(addr <= 8*1024*1024 - (1+tiles*NPASS)*24);
    const volatile uint32_t *r=(const volatile uint32_t *)(PVR_RAM_BASE+addr);
    CHECK(r[0] == 0x10000000);
    r+=6;
    uint32_t base=r[1];
    CHECK(!(base & 31) && base < 8*1024*1024 - tiles*NPASS*64);
    for(unsigned x=0;x<cols;++x) for(unsigned y=0;y<rows;++y)
        for(unsigned p=0;p<NPASS;++p,r+=6) {
            uint32_t ctrl=(x<<2)|(y<<8)|(p?0x40000000u:0)|
                (p+1<NPASS?0x10000000u:0);
            if(x+1==cols && y+1==rows && p+1==NPASS) ctrl|=0x80000000u;
            CHECK(r[0]==ctrl);
            for(unsigned l=0;l<5;++l)
                CHECK(r[1+l]==(l==(unsigned)list_at(p)
                    ? base+64*(p*tiles+y*cols+x) : 0x80000000u));
        }
    return base;
}

static unsigned check_image(const vid_framebuffer_info_t *fb) {
    unsigned mismatch=0, probes=0;
    CHECK(fb->pixel_mode==PM_RGB565 && fb->width==W && fb->height==H);
    for(unsigned y=0;y<H;++y) {
        const volatile uint16_t *row=(const volatile uint16_t *)
            ((const volatile uint8_t *)fb->address+y*fb->stride_bytes);
        for(unsigned x=0;x<W;++x) {
            uint16_t c=row[x];
            if(mode==BUFFERED) reference[frame][y][x]=c;
            else mismatch+=(reference[frame][y][x]!=c);
            /* Stay well away from triangle/tile boundaries. */
            if(y==208 && x%128==64) probes+=(c!=rgb565[(x/128+frame)%5]);
            if(y==170 && x%128==24) probes+=(c!=0xffff);
            if(y==208 && x%128==4) probes+=(c!=0x001f);
            if(y==340 && (x==220 || x==320 || x==420))
                probes+=(c!=(x==320?0xf800:0x07e0));
        }
    }
    printf("mode=%u frame=%u full_image_diff=%u probes_failed=%u/18\n",
           mode,frame,mismatch,probes);
    return probes+mismatch;
}

int main(void) {
    unsigned failures=0;
    const pvr_init_params_t params={.vertex_buf_size=512*1024,
        .dma_enabled=1,.opb_overflow_count=1};
    pvr_pass_config_t passes[NPASS]={0};
    for(unsigned p=0;p<NPASS;++p) passes[p].opb_sizes[list_at(p)]=PVR_BINSIZE_16;
    for(mode=BUFFERED;mode<=ASYNC_FLUSH;++mode) {
        uint32_t prevbase=0;
        CHECK(pvr_init_multipass(&params,passes,NPASS)==0);
        pvr_set_bg_color(0,0,1);
        for(unsigned p=0;p<NPASS;++p)
            if(mode!=MIXED_SQ || (p!=1 && p!=2))
                CHECK(pvr_set_pass_vertbuf_checked(p,list_at(p),staging[p],
                                                    sizeof(staging[p]),NULL)==0);
        int handler=pvr_event_handler_add(PVR_EVENT_REGISTRATION_COMPLETE|
            PVR_EVENT_RENDER_COMPLETE|PVR_EVENT_DISPLAY,event,NULL);
        CHECK(handler>=0);
        for(frame=0;frame<NFRAME;++frame) {
            pvr_render_ticket_t ticket;
            pvr_pipeline_status_t st;
            vid_framebuffer_info_t fb;
            unsigned a=registered,b=rendered,c=displayed;
            CHECK(pvr_wait_ready()==0);
            app_packet_bytes=0;
            pvr_scene_begin();
            for(pass=0;pass<NPASS;++pass) {
                draw_pass();
                if(pass+1<NPASS) CHECK(pvr_scene_next_pass()==0);
            }
            CHECK(pvr_scene_finish_tracked(&ticket)==0);
            CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_DISPLAYED,2000)==0);
            CHECK(pvr_get_pipeline_status(&st)==0 && !st.faults.mask);
            CHECK(st.registered_render_id==ticket.id && st.render_started_id==ticket.id &&
                  st.completed_render_id==ticket.id && st.displayed_render_id==ticket.id);
            CHECK(registered-a==1 && rendered-b==1 && displayed-c==1);
            uint32_t base=check_regions();
            CHECK(frame==0 || base!=prevbase);
            prevbase=base;
            printf("mode=%u frame=%u ticket=%" PRIu64 " passes=%u reg_done=%u "
                   "render_done=%u flips=%u start_id=%" PRIu64 " ta_start=%08" PRIx32
                   " ta_cursor=%08" PRIx32 " ta_limit=%08" PRIx32 "\n",
                   mode,frame,ticket.id,NPASS,registered-a,rendered-b,displayed-c,
                   st.render_started_id,reg_start,reg_cursor,reg_limit);
            CHECK(reg_start <= reg_cursor && reg_cursor <= reg_limit &&
                  reg_limit <= 8*1024*1024 && reg_limit-reg_start==(uint32_t)params.vertex_buf_size);
            if(reg_cursor != reg_start)
                printf("final_registration_ta_parameter_bytes=%" PRIu32 "\n",
                       reg_cursor-reg_start);
            else
                puts("final_registration_ta_parameter_bytes=unobserved (cursor equals start)");
#ifdef APERTURE_REQUIRE_TA_PROGRESS
            CHECK(reg_cursor > reg_start);
#endif
            CHECK(app_packet_bytes == 3744);
            printf("mode=%u frame=%u application_packet_bytes=%zu\n",
                   mode,frame,app_packet_bytes);
            CHECK(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED,&fb)==0);
            failures+=check_image(&fb);
        }
        CHECK(pvr_event_handler_remove(handler)==0);
        CHECK(pvr_shutdown()==0);
    }
    CHECK(!failures);
    puts("APERTURE_PASS: five windows, seven passes, mixed/sync/async images match");
    return 0;
}
