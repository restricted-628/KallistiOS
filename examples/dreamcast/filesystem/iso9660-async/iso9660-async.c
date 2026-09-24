/* KallistiOS ##version##

   Direct and arbitrary asynchronous ISO9660 read example.

   The default /cd/async.bin validation payload is expected to contain at
   least 4096 zero bytes. Passing another path exercises the same operations
   and guard checks without imposing that content requirement.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/fs_iso9660.h>

#include <inttypes.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t alignas(32) sectors[2 * 2048];
static uint8_t alignas(32) byte_buffer[3002];

#define REQUEST_WAIT_MS 6000u

static int wait_and_destroy_request(cdrom_request_t *request,
                                    const char *label,
                                    cdrom_request_status_t *status) {
    int failed = 0;

    if(cdrom_request_wait(request, REQUEST_WAIT_MS, status) < 0) {
        perror(label);
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, REQUEST_WAIT_MS, status) < 0) {
            perror("request cleanup wait");
            return -1;
        }
        failed = 1;
    }
    if(cdrom_request_wait_callback(request, REQUEST_WAIT_MS) < 0) {
        perror("request callback wait");
        return -1;
    }
    if(cdrom_request_destroy(request) < 0) {
        perror("request destroy");
        return -1;
    }
    return failed ? -1 : 0;
}

static void cancel_and_destroy_stream(cdrom_stream_session_t *stream) {
    cdrom_stream_session_status_t status;

    (void)cdrom_stream_session_cancel(stream);
    if(cdrom_stream_session_wait(stream, REQUEST_WAIT_MS, &status) == 0)
        (void)cdrom_stream_session_destroy(stream);
    else
        perror("stream cleanup wait");
}

static int expect_zero(const char *name, const uint8_t *buffer, size_t size) {
    size_t i;

    for(i = 0; i < size; ++i) {
        if(buffer[i] != 0) {
            printf("%s mismatch at %zu: expected 0, got %#x\n",
                   name, i, buffer[i]);
            return -1;
        }
    }

    return 0;
}

static void read_complete(cdrom_request_t *request,
                          const cdrom_request_status_t *status,
                          void *data) {
    (void)request;
    (void)data;

    printf("ISO read callback: state=%d result=%d data=%zu/%zu "
           "requested=%zu io=%zu/%zu sense=%#x/%#x/%#x\n",
           status->state, status->result, status->completed_bytes,
           status->data_bytes, status->requested_bytes,
           status->io_completed_bytes, status->io_bytes,
           status->sense.key, status->sense.asc, status->sense.ascq);
}

int main(int argc, char **argv) {
    iso9660_cache_stats_t cache_before;
    iso9660_cache_stats_t cache_after;
    cdrom_stream_session_status_t stream_status;
    cdrom_stream_session_t *stream;
    const char *path = argc > 1 ? argv[1] : "/cd/async.bin";
    bool verify_zero = argc <= 1;
    cdrom_request_status_t status;
    cdrom_request_t *request;
    file_t directory;
    file_t file;

    directory = fs_open("/cd", O_RDONLY | O_DIR);
    if(directory == FILEHND_INVALID) {
        perror("/cd");
        return 1;
    }

    request = fs_iso9660_prefetch_directory_async(
        directory, 5000, read_complete, NULL);
    fs_close(directory);
    if(!request) {
        perror("fs_iso9660_prefetch_directory_async");
        return 1;
    }

    if(wait_and_destroy_request(request, "directory prefetch wait",
                                &status) < 0) {
        return 1;
    }
    if(status.state != CDROM_REQUEST_COMPLETE) {
        printf("directory prefetch failed: state=%d result=%d errno=%d "
               "sense=%#x/%#x/%#x\n", status.state, status.result,
               status.error, status.sense.key, status.sense.asc,
               status.sense.ascq);
        return 1;
    }

    if(fs_iso9660_get_cache_stats(&cache_before) < 0) {
        perror("directory snapshot stats");
        return 1;
    }
    if(cache_before.directory_snapshot_entries == 0) {
        printf("directory prefetch completed without a resident snapshot\n");
        return 1;
    }

    file = fs_open(path, O_RDONLY);
    if(file == FILEHND_INVALID) {
        perror(path);
        return 1;
    }

    if(fs_iso9660_get_cache_stats(&cache_after) < 0
            || cache_after.directory_snapshot_hits
                <= cache_before.directory_snapshot_hits) {
        printf("prefetched root did not serve the file lookup\n");
        fs_close(file);
        return 1;
    }

    printf("ISO prefetch: entries=%zu bytes=%zu hits=%" PRIu64 "->%"
           PRIu64 "\n", cache_after.directory_snapshot_entries,
           cache_after.directory_snapshot_bytes,
           cache_before.directory_snapshot_hits,
           cache_after.directory_snapshot_hits);

    request = fs_iso9660_preseek_async(file, 5000, NULL, NULL);
    if(!request) {
        perror("fs_iso9660_preseek_async");
        fs_close(file);
        return 1;
    }

    if(wait_and_destroy_request(request, "preseek wait", &status) < 0) {
        fs_close(file);
        return 1;
    }

    printf("ISO preseek: state=%d result=%d errno=%d\n",
           status.state, status.result, status.error);
    if(status.state != CDROM_REQUEST_COMPLETE) {
        fs_close(file);
        return 1;
    }

    memset(sectors, 0xa5, sizeof(sectors));
    request = fs_iso9660_read_direct_async(file, sectors, 2, 5000,
                                           read_complete, NULL);
    if(!request) {
        perror("fs_iso9660_read_direct_async");
        fs_close(file);
        return 1;
    }

    while(cdrom_request_get_status(request, &status) == 0
            && (status.state == CDROM_REQUEST_QUEUED
                || status.state == CDROM_REQUEST_RUNNING)) {
        printf("ISO read progress: data=%zu/%zu requested=%zu io=%zu/%zu\n",
               status.completed_bytes, status.data_bytes,
               status.requested_bytes, status.io_completed_bytes,
               status.io_bytes);
        thd_sleep(100);
    }

    if(wait_and_destroy_request(request, "direct read wait", &status) < 0) {
        fs_close(file);
        return 1;
    }

    printf("ISO read final: state=%d result=%d errno=%d\n",
           status.state, status.result, status.error);

    if(status.state != CDROM_REQUEST_COMPLETE) {
        fs_close(file);
        return 1;
    }
    if(verify_zero
            && expect_zero("direct read", sectors, sizeof(sectors)) < 0) {
        fs_close(file);
        return 1;
    }

    if(fs_seek(file, 100, SEEK_SET) < 0) {
        perror("fs_seek");
        fs_close(file);
        return 1;
    }

    memset(byte_buffer, 0xa5, sizeof(byte_buffer));

    /* Deliberately use an unaligned destination and byte count. The ISO
       driver bounces only the portions that cannot receive direct GD DMA. */
    request = fs_iso9660_read_async(file, byte_buffer + 1, 3000, 5000,
                                    read_complete, NULL);
    if(!request) {
        perror("fs_iso9660_read_async");
        fs_close(file);
        return 1;
    }

    if(wait_and_destroy_request(request, "byte read wait", &status) < 0) {
        fs_close(file);
        return 1;
    }

    printf("ISO byte read: state=%d data=%zu/%zu io=%zu/%zu\n",
           status.state, status.completed_bytes, status.data_bytes,
           status.io_completed_bytes, status.io_bytes);

    if(status.state != CDROM_REQUEST_COMPLETE) {
        fs_close(file);
        return 1;
    }
    if(verify_zero
            && expect_zero("byte read", byte_buffer + 1, 3000) < 0) {
        fs_close(file);
        return 1;
    }
    if(byte_buffer[0] != 0xa5 || byte_buffer[3001] != 0xa5) {
        printf("byte read overwrote a destination guard\n");
        fs_close(file);
        return 1;
    }

    if(fs_seek(file, 0, SEEK_SET) < 0) {
        perror("staged seek");
        fs_close(file);
        return 1;
    }

    stream = fs_iso9660_stream_start(file, 2, 5000, 5000);
    if(!stream) {
        perror("fs_iso9660_stream_start");
        fs_close(file);
        return 1;
    }

    if(cdrom_stream_session_wait_ready(stream, 5000, &stream_status) < 0
            || stream_status.state != CDROM_STREAM_SESSION_READY) {
        perror("staged stream startup");
        cancel_and_destroy_stream(stream);
        fs_close(file);
        return 1;
    }

    memset(sectors, 0xa5, sizeof(sectors));
    request = cdrom_stream_session_transfer_async(
        stream, sectors, stream_status.total_bytes, 5000,
        read_complete, NULL);
    if(!request) {
        perror("cdrom_stream_session_transfer_async");
        cancel_and_destroy_stream(stream);
        fs_close(file);
        return 1;
    }

    if(wait_and_destroy_request(request, "staged transfer wait", &status)
            < 0) {
        cancel_and_destroy_stream(stream);
        fs_close(file);
        return 1;
    }

    if(cdrom_stream_session_wait(stream, 5000, &stream_status) < 0) {
        perror("staged stream completion");
        (void)cdrom_stream_session_cancel(stream);
        if(cdrom_stream_session_wait(
                stream, REQUEST_WAIT_MS, &stream_status) < 0) {
            perror("staged stream cleanup");
            fs_close(file);
            return 1;
        }
    }

    printf("ISO staged stream: state=%d data=%zu/%zu io=%zu/%zu\n",
           stream_status.state, stream_status.completed_bytes,
           stream_status.data_bytes, stream_status.transferred_bytes,
           stream_status.total_bytes);

    cdrom_stream_session_destroy(stream);
    fs_close(file);
    if(stream_status.state != CDROM_STREAM_SESSION_COMPLETE
            || (verify_zero
                && expect_zero("staged stream", sectors,
                               sizeof(sectors)) < 0))
        return 1;

    printf("KOS GD-ROM validation PASSED\n");
    return 0;
}
