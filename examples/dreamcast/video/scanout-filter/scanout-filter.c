/* KallistiOS ##version##

   scanout-filter.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static volatile unsigned int raster_events;
static volatile uint16_t raster_observed_line;

static void raster_callback(const vid_scanout_status_t *status,
                            void *user_data) {
    (void)user_data;
    raster_observed_line = status->scanline;
    ++raster_events;
}

static int expect_errno(int result, int expected, const char *operation) {
    if(result == -1 && errno == expected)
        return 0;

    printf("FAIL: %s returned %d with errno %d, expected %d\n",
           operation, result, errno, expected);
    return -1;
}

static int filters_equal(const vid_display_filter_t *a,
                         const vid_display_filter_t *b) {
    return a->dithering == b->dithering &&
           a->antialiasing == b->antialiasing &&
           a->vertical_scale == b->vertical_scale;
}

int main(void) {
    vid_mode_t mode;
    vid_scanout_status_t first;
    vid_scanout_status_t current;
    vid_display_filter_t original;
    vid_display_filter_t requested;
    vid_display_filter_t observed;
    uint64_t deadline;
    uint16_t raster_line;
    uint16_t configured_line;
    unsigned int stopped_count;
    int raster_handle;
    bool scanline_changed = false;

    printf("KallistiOS ##version##\n\n");

    errno = 0;
    if(expect_errno(vid_get_scanout_status(NULL), EFAULT,
                    "vid_get_scanout_status(NULL)") < 0)
        return 1;

    errno = 0;
    if(expect_errno(vid_get_display_filter(NULL), EFAULT,
                    "vid_get_display_filter(NULL)") < 0)
        return 1;

    errno = 0;
    if(expect_errno(vid_set_display_filter(NULL), EFAULT,
                    "vid_set_display_filter(NULL)") < 0)
        return 1;

    if(vid_get_scanout_status(&first) < 0) {
        perror("vid_get_scanout_status");
        return 1;
    }

    deadline = timer_ms_gettime64() + 100;
    do {
        if(vid_get_scanout_status(&current) < 0) {
            perror("vid_get_scanout_status");
            return 1;
        }

        if(current.scanline != first.scanline || current.field != first.field) {
            scanline_changed = true;
            break;
        }

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    if(!scanline_changed) {
        puts("FAIL: scanout timing did not advance within 100 ms");
        return 1;
    }

    if(vid_get_display_filter(&original) < 0) {
        perror("vid_get_display_filter");
        return 1;
    }

    if(original.vertical_scale < 0x100) {
        printf("FAIL: implausible active vertical-scale coefficient %u\n",
               original.vertical_scale);
        return 1;
    }

    requested = original;
    requested.vertical_scale = 0;
    errno = 0;
    if(expect_errno(vid_set_display_filter(&requested), EINVAL,
                    "zero vertical scale") < 0)
        return 1;

    if(vid_get_display_filter(&observed) < 0 ||
       !filters_equal(&observed, &original)) {
        puts("FAIL: rejected filter update changed hardware state");
        return 1;
    }

    requested = original;
    requested.dithering = !original.dithering;
    if(vid_set_display_filter(&requested) < 0 ||
       vid_get_display_filter(&observed) < 0 ||
       !filters_equal(&observed, &requested)) {
        puts("FAIL: dithering update did not preserve unrelated fields");
        (void)vid_set_display_filter(&original);
        return 1;
    }

    requested = original;
    requested.antialiasing = !original.antialiasing;
    if(vid_set_display_filter(&requested) < 0 ||
       vid_get_display_filter(&observed) < 0 ||
       !filters_equal(&observed, &requested)) {
        puts("FAIL: antialiasing update did not preserve unrelated fields");
        (void)vid_set_display_filter(&original);
        return 1;
    }

    if(vid_set_display_filter(&original) < 0 ||
       vid_get_display_filter(&observed) < 0 ||
       !filters_equal(&observed, &original)) {
        puts("FAIL: original filter state was not restored");
        return 1;
    }

    if(vid_get_mode(&mode) < 0) {
        perror("vid_get_mode");
        return 1;
    }

    errno = 0;
    if(expect_errno(vid_raster_get_scanline(NULL), EFAULT,
                    "vid_raster_get_scanline(NULL)") < 0)
        return 1;

    errno = 0;
    if(expect_errno(vid_raster_set_scanline(mode.scanlines + 1u), ERANGE,
                    "unreachable raster line") < 0)
        return 1;

    raster_line = mode.scanlines / 2u;
    if(vid_raster_set_scanline(raster_line) < 0 ||
       vid_raster_get_scanline(&configured_line) < 0 ||
       configured_line != raster_line) {
        puts("FAIL: raster line did not round-trip");
        return 1;
    }

    errno = 0;
    if(expect_errno(vid_raster_handler_add(NULL, NULL), EINVAL,
                    "vid_raster_handler_add(NULL)") < 0)
        return 1;

    raster_handle = vid_raster_handler_add(raster_callback, NULL);
    if(raster_handle < 0) {
        perror("vid_raster_handler_add");
        return 1;
    }

    deadline = timer_ms_gettime64() + 150;
    while(raster_events < 2 && timer_ms_gettime64() < deadline)
        thd_pass();

    if(raster_events < 2 || raster_observed_line > mode.scanlines) {
        printf("FAIL: raster callback count=%u observed line=%u\n",
               raster_events, raster_observed_line);
        (void)vid_raster_handler_remove(raster_handle);
        return 1;
    }

    if(vid_raster_handler_remove(raster_handle) < 0) {
        perror("vid_raster_handler_remove");
        return 1;
    }

    errno = 0;
    if(expect_errno(vid_raster_handler_remove(raster_handle), ENOENT,
                    "stale raster handler") < 0)
        return 1;

    stopped_count = raster_events;
    thd_sleep(50);
    if(raster_events != stopped_count) {
        puts("FAIL: removed raster callback continued firing");
        return 1;
    }

    printf("Scanout advanced from line %u field %u to line %u field %u\n",
           first.scanline, first.field, current.scanline, current.field);
    printf("Filter: dither=%u antialias=%u vertical coefficient=%u\n",
           original.dithering, original.antialiasing,
           original.vertical_scale);
    printf("Raster: configured line=%u observed line=%u events=%u\n",
           raster_line, raster_observed_line, stopped_count);
    puts("PASS: scanout, display-filter, and raster validation complete");
    return 0;
}
