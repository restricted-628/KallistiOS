/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

static unsigned checks, failures, mode_calls, query_calls, init_calls, read_calls;
static unsigned submissions;
static bool testing, held, lock_fail, query_fail, mode_fail, submit_fail, no_disc;
static cd_disc_types_t disc_type = CD_CDROM_XA;
static cd_sec_mode_params_t last_mode;
static _Alignas(32) uint8_t buffer[20 * 2352];
static int token, callback_data;

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("BIOS-READ-CONTRACT: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

int __real_g1_bus_lock(void);
int __real_g1_bus_unlock(void);

int __wrap_g1_bus_lock(void) {
    if(!testing)
        return __real_g1_bus_lock();
    CHECK(!held);
    if(lock_fail) {
        errno = EIO;
        return -1;
    }
    held = true;
    return 0;
}

int __wrap_g1_bus_unlock(void) {
    if(!testing)
        return __real_g1_bus_unlock();
    CHECK(held);
    held = false;
    return 0;
}

int __wrap_syscall_gdrom_check_drive(cd_check_drive_status_t *out) {
    ++query_calls;
    CHECK(testing && held);
    if(query_fail) {
        errno = EIO;
        return -1; /* Deliberately leave output untouched. */
    }
    out->disc_type = disc_type;
    return 0;
}

int __wrap_syscall_gdrom_sector_mode(cd_sec_mode_params_t *mode) {
    ++mode_calls;
    CHECK(testing && held && mode->rw == 0);
    last_mode = *mode;
    if(mode_fail) {
        errno = EIO;
        return -1;
    }
    return 0;
}

gdc_cmd_hnd_t __wrap_syscall_gdrom_send_command(cd_cmd_code_t cmd, void *params) {
    CHECK(testing && held);
    if(cmd == CD_CMD_INIT) {
        ++init_calls;
        CHECK(!params);
    }
    else {
        cd_read_params_t *read = params;
        ++read_calls;
        CHECK(cmd == CD_CMD_PIOREAD && read->start_sec == 150
              && read->num_sec == 17 && read->buffer == buffer && !read->is_test);
    }
    return 77;
}

void __wrap_syscall_gdrom_exec_server(void) {
    CHECK(testing && held);
}

cd_cmd_chk_t __wrap_syscall_gdrom_check_command(
    gdc_cmd_hnd_t hnd, cd_cmd_chk_status_t *out) {
    CHECK(testing && held && hnd == 77);
    memset(out, 0, sizeof(*out));
    if(no_disc) {
        out->err1 = CDROM_SENSE_NOT_READY;
        out->err2 = 0x3a;
        return CD_CMD_FAILED;
    }
    return CD_CMD_COMPLETED;
}

static void callback(cdrom_request_t *request,
                     const cdrom_request_status_t *status, void *data) {
    (void)request;
    (void)status;
    (void)data;
    CHECK(false); /* The submission spy must not dispatch callbacks. */
}

cdrom_request_t *__wrap_cdrom_request_submit_dma_chain(
    const cdrom_request_dma_segment_t *first, size_t requested_bytes,
    size_t data_bytes, size_t io_bytes, uint32_t timeout,
    cdrom_request_continue_t continuation, void *continuation_data,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t cb, void *data) {
    size_t bytes = 17 * cdrom_sector_size_internal();
    ++submissions;
    CHECK(first->buffer == buffer && first->params.start_sec == 150
          && first->params.num_sec == 17 && !first->params.is_test
          && first->params.buffer == (void *)((uintptr_t)buffer & MEM_AREA_CACHE_MASK));
    CHECK(first->io_bytes == bytes && first->data_bytes == bytes
          && first->data_offset == 0 && first->data_direct && first->cacheable
          && requested_bytes == bytes && data_bytes == bytes && io_bytes == bytes);
    CHECK(timeout == 0 && !continuation && !continuation_data
          && !finalizer && !finalizer_data && cb == callback && data == &callback_data);
    errno = ENOMEM;
    return submit_fail ? NULL : (cdrom_request_t *)&token;
}

int main(void) {
    unsigned before;
    testing = true;
    CHECK(fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT);
    CHECK(cdrom_sector_size_internal() == 2048);
    CHECK(cdrom_bios_change_datatype(CDROM_READ_DEFAULT, -1, 2352) == 0);
    CHECK(last_mode.sector_part == CDROM_READ_WHOLE_SECTOR
          && last_mode.track_type == 0 && last_mode.sector_size == 2352
          && cdrom_sector_size_internal() == 2352 && query_calls == 0);

    mode_fail = true;
    CHECK(cdrom_bios_change_datatype(CDROM_READ_DATA_AREA, 1024, 2048) == -1);
    CHECK(cdrom_sector_size_internal() == 2352 && !held);
    CHECK(cdrom_bios_change_datatype(CDROM_READ_DATA_AREA, 1024, 2048) == -1);
    CHECK(cdrom_sector_size_internal() == 2352 && !held);
    mode_fail = false;
    query_fail = true;
    before = mode_calls;
    CHECK(cdrom_bios_change_datatype(CDROM_READ_DEFAULT, -1, -1) == -1
          && errno == EIO && mode_calls == before
          && cdrom_sector_size_internal() == 2352 && !held);
    query_fail = false;
    lock_fail = true;
    CHECK(cdrom_bios_change_datatype(CDROM_READ_DEFAULT, -1, -1) == ERR_HARDWARE
          && mode_calls == before && cdrom_sector_size_internal() == 2352);
    lock_fail = false;

    /* BIOS selection is explicit even after generic reads switch to direct.
       No transfer occurs: firmware and queue are spies. */
    CHECK(cdrom_bios_read_sectors(buffer, 150, 17) == ERR_OK);
    CHECK(cdrom_bios_read_sectors(buffer, 150, 17) == ERR_OK);
    CHECK(cdrom_bios_read_sectors_ex(buffer, 150, 17, false) == ERR_OK);
    CHECK(cdrom_bios_read_sectors_ex(buffer, 150, 17, false) == ERR_OK);
    CHECK(read_calls == 4);
    CHECK(cdrom_bios_read_sectors_async(buffer, 150, 17, 0, callback, &callback_data)
          == (cdrom_request_t *)&token);
    CHECK(cdrom_bios_read_sectors_async(buffer, 150, 17, 0, callback, &callback_data)
          == (cdrom_request_t *)&token);
    submit_fail = true;
    CHECK(!cdrom_bios_read_sectors_async(buffer, 150, 17, 0, callback, &callback_data)
          && errno == ENOMEM);
    CHECK(!cdrom_bios_read_sectors_async(buffer + 1, 150, 17, 0, callback, &callback_data)
          && errno == EINVAL && submissions == 3);
    submit_fail = false;

    CHECK(cdrom_bios_reinit() == 0 && init_calls == 1);
    CHECK(last_mode.sector_part == CDROM_READ_DATA_AREA && last_mode.track_type == 2048
          && last_mode.sector_size == 2048 && cdrom_sector_size_internal() == 2048);
    disc_type = CD_CDROM;
    CHECK(cdrom_bios_reinit() == 0 && init_calls == 2 && last_mode.track_type == 1024);
    CHECK(cdrom_bios_reinit_ex(CDROM_READ_WHOLE_SECTOR, 0, 2352) == 0
          && init_calls == 3 && cdrom_sector_size_internal() == 2352);
    CHECK(cdrom_bios_reinit_ex(CDROM_READ_DATA_AREA, 1024, 2048) == 0 && init_calls == 4);
    CHECK(cdrom_bios_set_sector_size(2352) == 0 && init_calls == 5
          && cdrom_sector_size_internal() == 2352);
    CHECK(cdrom_bios_set_sector_size(2048) == 0 && init_calls == 6
          && cdrom_sector_size_internal() == 2048);
    CHECK(cdrom_bios_read_sectors_async(buffer, 150, 17, 0, callback, &callback_data)
          == (cdrom_request_t *)&token && submissions == 4);
    no_disc = true;
    before = mode_calls;
    CHECK(cdrom_bios_reinit() == ERR_NO_DISC && init_calls == 7
          && mode_calls == before && !held);
    testing = false;
    printf("BIOS-READ-CONTRACT: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
