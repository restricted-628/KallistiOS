/* KallistiOS ##version##

   Direct ISO9660 backend validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <kos/fs.h>
#include <kos/init.h>
#include <kos/thread.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define SECTOR_COUNT 16u
#define SECTOR_BYTES (SECTOR_COUNT * 2048u)
#define BYTE_OFFSET  37u
#define BYTE_COUNT   5000u
#define BYTE_IO      6144u
#define GUARD_BYTES  32u
#define GUARD_VALUE  0xa5u

_Alignas(32) static uint8_t sync_sectors[SECTOR_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t async_sectors[SECTOR_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t stream_sectors[SECTOR_BYTES + GUARD_BYTES];
static uint8_t sync_bytes[GUARD_BYTES + 3 + BYTE_COUNT + GUARD_BYTES];
static uint8_t async_bytes[GUARD_BYTES + 3 + BYTE_COUNT + GUARD_BYTES];
static volatile bool callback_seen;
static volatile bool callback_coherent;

static bool filled_with(const uint8_t *data, size_t bytes, uint8_t value) {
    size_t i;

    for(i = 0; i < bytes; ++i) {
        if(data[i] != value)
            return false;
    }
    return true;
}

static void sector_complete(cdrom_request_t *request,
                            const cdrom_request_status_t *status, void *data) {
    (void)request;
    (void)data;

    callback_coherent = status->backend == CDROM_REQUEST_BACKEND_DIRECT
        && status->state == CDROM_REQUEST_COMPLETE
        && status->completed_bytes == SECTOR_BYTES
        && status->io_completed_bytes == SECTOR_BYTES;
    callback_seen = true;
}

static int wait_and_destroy(cdrom_request_t *request,
                            cdrom_request_status_t *status,
                            bool wait_callback) {
    int saved_errno;

    if(cdrom_request_wait(request, 8000, status) < 0)
        goto fail;
    if(wait_callback && cdrom_request_wait_callback(request, 8000) < 0)
        goto fail;
    if(cdrom_request_destroy(request) < 0)
        return -1;
    return 0;

fail:
    saved_errno = errno;
    (void)cdrom_request_cancel(request);
    if(cdrom_request_wait(request, 2000, status) == 0
            && (!wait_callback
                || cdrom_request_wait_callback(request, 2000) == 0))
        (void)cdrom_request_destroy(request);
    errno = saved_errno;
    return -1;
}

int main(int argc, char **argv) {
    cdrom_drive_state_t drive_state = { 0 };
    cdrom_request_status_t sector_status = { 0 };
    cdrom_request_status_t byte_status = { 0 };
    cdrom_request_status_t noop_status = { 0 };
    cdrom_request_status_t preseek_status = { 0 };
    cdrom_request_status_t stream_transfer_status = { 0 };
    cdrom_stream_session_status_t stream_ready_status = { 0 };
    cdrom_stream_session_status_t stream_status = { 0 };
    cdrom_stream_session_status_t idle_status = { 0 };
    cdrom_request_t *request;
    cdrom_stream_session_t *stream = NULL;
    file_t fd = FILEHND_INVALID;
    uint8_t *sync_byte_data = sync_bytes + GUARD_BYTES + 3;
    uint8_t *async_byte_data = async_bytes + GUARD_BYTES + 3;
    ssize_t sync_read = -1;
    ssize_t byte_read = -1;
    int request_error = 0;
    bool backend_locked = false;
    bool monitor_direct = false;
    bool sector_guards;
    bool byte_guards;
    bool passed;

    (void)argc;
    (void)argv;

    memset(sync_sectors, GUARD_VALUE, sizeof(sync_sectors));
    memset(async_sectors, GUARD_VALUE, sizeof(async_sectors));
    memset(stream_sectors, GUARD_VALUE, sizeof(stream_sectors));
    memset(sync_bytes, GUARD_VALUE, sizeof(sync_bytes));
    memset(async_bytes, GUARD_VALUE, sizeof(async_bytes));

    puts("Direct ISO9660 backend validation");
    if(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) < 0) {
        request_error = errno;
        goto done;
    }

    for(unsigned int i = 0; i < 100; ++i) {
        if(cdrom_get_cached_drive_state(&drive_state) == 0
                && drive_state.backend == CDROM_REQUEST_BACKEND_DIRECT) {
            monitor_direct = true;
            break;
        }
        thd_sleep(20);
    }

    fd = fs_open("/cd/1ST_READ.BIN", O_RDONLY);
    if(fd == FILEHND_INVALID) {
        request_error = errno;
        printf("open failed: %s (%d)\n", strerror(errno), errno);
        goto done;
    }

    errno = 0;
    backend_locked = fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS) < 0
        && errno == EBUSY;

    request = fs_iso9660_preseek_async(fd, 4000, NULL, NULL);
    if(!request || wait_and_destroy(request, &preseek_status, false) < 0) {
        request_error = errno;
        goto done;
    }

    sync_read = fs_read(fd, sync_sectors, SECTOR_BYTES);
    if(sync_read != SECTOR_BYTES || fs_seek(fd, 0, SEEK_SET) < 0) {
        request_error = errno ? errno : EIO;
        goto done;
    }

    request = fs_iso9660_read_direct_async(
        fd, async_sectors, SECTOR_COUNT, 6000, sector_complete, NULL);
    if(!request || wait_and_destroy(request, &sector_status, true) < 0) {
        request_error = errno;
        goto done;
    }

    if(fs_seek(fd, 0, SEEK_SET) < 0
            || !(stream = fs_iso9660_stream_start(fd, SECTOR_COUNT,
                                                  6000, 2000))
            || cdrom_stream_session_wait_ready(
                stream, 8000, &stream_ready_status) < 0
            || stream_ready_status.state != CDROM_STREAM_SESSION_READY) {
        request_error = errno ? errno : EIO;
        goto done;
    }

    request = cdrom_stream_session_transfer_async(
        stream, stream_sectors, 4u * 2048u, 4000, NULL, NULL);
    if(!request
            || wait_and_destroy(request, &stream_transfer_status, false) < 0) {
        request_error = errno;
        goto done;
    }

    request = cdrom_stream_session_transfer_async(
        stream, stream_sectors + 4u * 2048u,
        SECTOR_BYTES - 4u * 2048u, 6000, NULL, NULL);
    if(!request
            || wait_and_destroy(request, &stream_transfer_status, false) < 0
            || cdrom_stream_session_wait(stream, 8000, &stream_status) < 0
            || cdrom_stream_session_destroy(stream) < 0) {
        request_error = errno;
        goto done;
    }
    stream = NULL;

    if(fs_seek(fd, 0, SEEK_SET) < 0
            || !(stream = fs_iso9660_stream_start(fd, 1, 6000, 100))
            || cdrom_stream_session_wait_ready(stream, 8000, NULL) < 0
            || cdrom_stream_session_wait(stream, 4000, &idle_status) < 0
            || cdrom_stream_session_destroy(stream) < 0) {
        request_error = errno;
        goto done;
    }
    stream = NULL;

    if(fs_seek(fd, BYTE_OFFSET, SEEK_SET) < 0
            || (byte_read = fs_read(fd, sync_byte_data, BYTE_COUNT))
                != BYTE_COUNT
            || fs_seek(fd, BYTE_OFFSET, SEEK_SET) < 0) {
        request_error = errno ? errno : EIO;
        goto done;
    }

    request = fs_iso9660_read_async(
        fd, async_byte_data, BYTE_COUNT, 6000, NULL, NULL);
    if(!request || wait_and_destroy(request, &byte_status, false) < 0) {
        request_error = errno;
        goto done;
    }

    request = fs_iso9660_read_async(fd, NULL, 0, 0, NULL, NULL);
    if(!request || wait_and_destroy(request, &noop_status, false) < 0) {
        request_error = errno;
        goto done;
    }

done:
    if(stream) {
        (void)cdrom_stream_session_cancel(stream);
        (void)cdrom_stream_session_wait(stream, 3000, &stream_status);
        (void)cdrom_stream_session_destroy(stream);
    }
    if(fd != FILEHND_INVALID)
        fs_close(fd);

    sector_guards = filled_with(sync_sectors + SECTOR_BYTES, GUARD_BYTES,
                                GUARD_VALUE)
        && filled_with(async_sectors + SECTOR_BYTES, GUARD_BYTES, GUARD_VALUE);
    sector_guards = sector_guards
        && filled_with(stream_sectors + SECTOR_BYTES, GUARD_BYTES,
                       GUARD_VALUE);
    byte_guards = filled_with(sync_bytes, GUARD_BYTES + 3, GUARD_VALUE)
        && filled_with(async_bytes, GUARD_BYTES + 3, GUARD_VALUE)
        && filled_with(sync_byte_data + BYTE_COUNT, GUARD_BYTES, GUARD_VALUE)
        && filled_with(async_byte_data + BYTE_COUNT, GUARD_BYTES, GUARD_VALUE);

    passed = request_error == 0
        && fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT
        && backend_locked && monitor_direct && sync_read == SECTOR_BYTES
        && preseek_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && preseek_status.command == CD_CMD_SEEK
        && preseek_status.state == CDROM_REQUEST_COMPLETE
        && sector_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && sector_status.state == CDROM_REQUEST_COMPLETE
        && sector_status.completed_bytes == SECTOR_BYTES
        && sector_status.io_completed_bytes == SECTOR_BYTES
        && callback_seen && callback_coherent
        && memcmp(sync_sectors, async_sectors, SECTOR_BYTES) == 0
        && stream_ready_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && stream_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && stream_status.state == CDROM_STREAM_SESSION_COMPLETE
        && stream_status.completed_bytes == SECTOR_BYTES
        && stream_status.transferred_bytes == SECTOR_BYTES
        && stream_transfer_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && stream_transfer_status.state == CDROM_REQUEST_COMPLETE
        && memcmp(sync_sectors, stream_sectors, SECTOR_BYTES) == 0
        && idle_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && idle_status.state == CDROM_STREAM_SESSION_TIMED_OUT
        && idle_status.result == ERR_TIMEOUT
        && byte_read == BYTE_COUNT
        && byte_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && byte_status.state == CDROM_REQUEST_COMPLETE
        && byte_status.requested_bytes == BYTE_COUNT
        && byte_status.data_bytes == BYTE_COUNT
        && byte_status.completed_bytes == BYTE_COUNT
        && byte_status.io_bytes == BYTE_IO
        && byte_status.io_completed_bytes == BYTE_IO
        && memcmp(sync_byte_data, async_byte_data, BYTE_COUNT) == 0
        && noop_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && noop_status.state == CDROM_REQUEST_COMPLETE
        && sector_guards && byte_guards;

    printf("%s: backend=%d monitor=%d seek=%d locked=%d error=%d\n",
           passed ? "PASS" : "FAIL", fs_iso9660_get_backend(),
           monitor_direct, preseek_status.state,
           backend_locked, request_error);
    printf("sector=%ld state=%d bytes=%lu/%lu callback=%d/%d match=%d\n",
           (long)sync_read, sector_status.state,
           (unsigned long)sector_status.completed_bytes,
           (unsigned long)sector_status.io_completed_bytes,
           callback_seen, callback_coherent,
           memcmp(sync_sectors, async_sectors, SECTOR_BYTES) == 0);
    printf("stream=%d backend=%d bytes=%lu/%lu match=%d idle=%d/%d\n",
           stream_status.state, stream_status.backend,
           (unsigned long)stream_status.completed_bytes,
           (unsigned long)stream_status.transferred_bytes,
           memcmp(sync_sectors, stream_sectors, SECTOR_BYTES) == 0,
           idle_status.state, idle_status.result);
    printf("byte=%ld state=%d data=%lu io=%lu match=%d noop=%d guards=%d/%d\n",
           (long)byte_read, byte_status.state,
           (unsigned long)byte_status.completed_bytes,
           (unsigned long)byte_status.io_completed_bytes,
           memcmp(sync_byte_data, async_byte_data, BYTE_COUNT) == 0,
           noop_status.backend, sector_guards, byte_guards);

    for(;;)
        thd_sleep(1000);
}
