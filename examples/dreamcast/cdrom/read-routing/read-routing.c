/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);
static unsigned checks, failures, probes, resets, reads, queued, bios_modes, bios_commands;
static bool testing, held, missing_status, reset_failed;
static gdrom_direct_probe_command_t failure_stage = GDROM_DIRECT_PROBE_REQ_STAT;
static int transport_error, read_error, media_result = ERR_OK;
static cd_disc_types_t disc = CD_CDROM_XA;
static gdrom_direct_sector_type_t expected_type, captured_type;
static _Alignas(32) uint8_t buffer[34 * 2352];
static int token;

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("READ-ROUTING: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

static void fill_probe(gdrom_direct_probe_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->result = media_result;
    out->status_valid = !missing_status;
    out->status.disc_type = disc;
    out->last_command = failure_stage;
    gdrom_direct_result_t *trace = failure_stage == GDROM_DIRECT_PROBE_TEST_UNIT
        ? &out->test_unit_transport : failure_stage == GDROM_DIRECT_PROBE_REQ_ERROR
        ? &out->error_transport : &out->status_transport;
    trace->sense_valid = transport_error == EIO;
    trace->sense.key = CDROM_SENSE_ILLEGAL_REQUEST;
    trace->sense.asc = 0x24;
}
int __wrap_gdrom_direct_probe(gdrom_direct_probe_result_t *out, uint32_t timeout) {
    CHECK(testing && !held && timeout == 10000);
    ++probes; fill_probe(out);
    if(transport_error) { errno = transport_error; return -1; }
    return 0;
}
int __wrap_gdrom_direct_reinitialize(gdrom_direct_reinit_result_t *out, uint32_t timeout) {
    CHECK(testing && !held && timeout == 10000);
    ++resets; memset(out, 0, sizeof(*out));
    out->reset_transport.recovery_succeeded = !reset_failed;
    fill_probe(&out->probe);
    if(transport_error) { errno = transport_error; return -1; }
    return 0;
}
static int read_call(void *dest, uint32_t fad, size_t count,
        gdrom_direct_sector_type_t type, uint32_t timeout,
        gdrom_direct_result_t *out, bool dma) {
    CHECK(testing && !held && dest == buffer && fad == 150 && count == 17);
    CHECK(type == expected_type && timeout == 10000 && out);
    ++reads;
    memset(out, 0, sizeof(*out));
    if(dma && type == GDROM_DIRECT_SECTOR_RAW2352) { errno = EINVAL; return -1; }
    if(read_error) { errno = read_error; return -1; }
    return 0;
}
int __wrap_gdrom_direct_read_sectors(void *dest, uint32_t fad, size_t count,
        gdrom_direct_sector_type_t type, uint32_t timeout, gdrom_direct_result_t *out) {
    return read_call(dest, fad, count, type, timeout, out, false);
}
int __wrap_gdrom_direct_read_sectors_dma(void *dest, uint32_t fad, size_t count,
        gdrom_direct_sector_type_t type, uint32_t timeout, gdrom_direct_result_t *out) {
    return read_call(dest, fad, count, type, timeout, out, true);
}
static void callback(cdrom_request_t *request, const cdrom_request_status_t *status, void *data) {
    (void)request; (void)status; (void)data; CHECK(false);
}
cdrom_request_t *__wrap_gdrom_direct_read_sectors_dma_async(void *dest, uint32_t fad,
        size_t count, gdrom_direct_sector_type_t type, uint32_t timeout,
        gdrom_direct_result_t *out, cdrom_request_callback_t cb, void *data) {
    CHECK(testing && !held && dest == buffer && fad == 150 && count == 17);
    CHECK(type == expected_type && !out && cb == callback && data == &token);
    ++queued; captured_type = type;
    if(!timeout || type == GDROM_DIRECT_SECTOR_RAW2352) { errno = EINVAL; return NULL; }
    CHECK(timeout == 1234);
    if(read_error) { errno = read_error; return NULL; }
    return (cdrom_request_t *)&token;
}
int __real_g1_bus_lock(void);
int __real_g1_bus_unlock(void);
int __wrap_g1_bus_lock(void) {
    if(!testing) return __real_g1_bus_lock();
    CHECK(!held); held = true; return 0;
}
int __wrap_g1_bus_unlock(void) {
    if(!testing) return __real_g1_bus_unlock();
    CHECK(held); held = false; return 0;
}
int __wrap_syscall_gdrom_sector_mode(cd_sec_mode_params_t *mode) {
    CHECK(testing && held && mode->rw == 0);
    ++bios_modes; return 0;
}
gdc_cmd_hnd_t __wrap_syscall_gdrom_send_command(cd_cmd_code_t command, void *params) {
    (void)command; (void)params; ++bios_commands; CHECK(false); return 0;
}

static void expect_reads(gdrom_direct_sector_type_t type) {
    unsigned before_probe = probes, before_reset = resets;
    expected_type = type;
    CHECK(cdrom_read_sectors(buffer, 150, 17) == ERR_OK);
    CHECK(cdrom_read_sectors_ex(buffer, 150, 17, false) == ERR_OK);
    CHECK(cdrom_read_sectors_ex(buffer, 150, 17, true)
          == (type == GDROM_DIRECT_SECTOR_RAW2352 ? ERR_SYS : ERR_OK));
    CHECK(cdrom_read_sectors_async(buffer, 150, 17, 1234, callback, &token)
          == (type == GDROM_DIRECT_SECTOR_RAW2352 ? NULL : (cdrom_request_t *)&token));
    CHECK(probes == before_probe && resets == before_reset && !bios_commands);
}
int main(void) {
    testing = true;
    CHECK(!fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS));
    expect_reads(GDROM_DIRECT_SECTOR_MODE1);
    CHECK(!fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT));
    CHECK(!cdrom_change_datatype(CDROM_READ_DATA_AREA, 2048, 2048));
    CHECK(!probes && !resets && !bios_modes);
    expect_reads(GDROM_DIRECT_SECTOR_MODE2_FORM1);
    /* Changing the generic format does not rewrite an already submitted format. */
    CHECK(captured_type == GDROM_DIRECT_SECTOR_MODE2_FORM1);
    CHECK(!cdrom_change_datatype(CDROM_READ_DEFAULT, -1, 2352));
    CHECK(captured_type == GDROM_DIRECT_SECTOR_MODE2_FORM1 && !probes);
    expect_reads(GDROM_DIRECT_SECTOR_RAW2352);
    CHECK(cdrom_sector_size_internal() == 2048);
    CHECK(!cdrom_bios_change_datatype(CDROM_READ_WHOLE_SECTOR, 0, 2352));
    CHECK(bios_modes == 1 && cdrom_sector_size_internal() == 2352);
    CHECK(!cdrom_change_datatype(CDROM_READ_DEFAULT, 1024, -1));
    expect_reads(GDROM_DIRECT_SECTOR_MODE1);
    CHECK(cdrom_sector_size_internal() == 2352);
    CHECK(!cdrom_bios_change_datatype(CDROM_READ_DATA_AREA, 2048, 2048));
    expect_reads(GDROM_DIRECT_SECTOR_MODE1);
    CHECK(!cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1) && probes == 1);
    expect_reads(GDROM_DIRECT_SECTOR_MODE2_FORM1);
    const int bad[][3] = {
        {CDROM_READ_DATA_AREA, 0, 2048}, {CDROM_READ_DEFAULT, -2, 2048},
        {CDROM_READ_WHOLE_SECTOR, 1024, 2352}, {CDROM_READ_DATA_AREA, -1, 2352},
        {CDROM_READ_WHOLE_SECTOR, -1, 2048}, {CDROM_READ_DEFAULT, -1, 2336},
        {CDROM_READ_DEFAULT, -1, 0}, {99, -1, 2048}
    };
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        unsigned p = probes, r = resets;
        CHECK(cdrom_change_datatype(bad[i][0], bad[i][1], bad[i][2]) == ERR_SYS && errno == ENOTSUP);
        CHECK(cdrom_reinit_ex(bad[i][0], bad[i][1], bad[i][2]) == ERR_SYS && errno == ENOTSUP);
        CHECK(probes == p && resets == r);
    }
    transport_error = ETIMEDOUT;
    CHECK(cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1) == ERR_TIMEOUT);
    CHECK(cdrom_reinit() == ERR_TIMEOUT);
    transport_error = EIO;
    for(unsigned stage = GDROM_DIRECT_PROBE_TEST_UNIT; stage <= GDROM_DIRECT_PROBE_REQ_STAT; ++stage) {
        failure_stage = stage;
        CHECK(cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1) == ERR_ILLEGAL_REQUEST);
        CHECK(cdrom_reinit() == ERR_ILLEGAL_REQUEST);
    }
    reset_failed = true;
    CHECK(cdrom_reinit() == ERR_SYS); /* Do not use a probe trace if reset failed. */
    reset_failed = false;
    transport_error = 0;
    media_result = ERR_NO_DISC;
    CHECK(cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1) == ERR_NO_DISC && errno == ENODEV);
    CHECK(cdrom_reinit_ex(CDROM_READ_DEFAULT, -1, 2352) == ERR_NO_DISC);
    media_result = ERR_BUSY;
    CHECK(cdrom_reinit() == ERR_BUSY);
    media_result = ERR_OK; missing_status = true;
    CHECK(cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1) == ERR_SYS && errno == EPROTO);
    CHECK(cdrom_reinit() == ERR_SYS && errno == EPROTO);
    missing_status = false;
    expect_reads(GDROM_DIRECT_SECTOR_MODE2_FORM1);
    disc = CD_CDROM;
    unsigned p = probes, r = resets;
    CHECK(!cdrom_reinit() && resets == r + 1 && probes == p);
    expect_reads(GDROM_DIRECT_SECTOR_MODE1);
    disc = CD_CDROM_XA;
    CHECK(!cdrom_set_sector_size(2048));
    expect_reads(GDROM_DIRECT_SECTOR_MODE2_FORM1);
    CHECK(!cdrom_set_sector_size(2352));
    expect_reads(GDROM_DIRECT_SECTOR_RAW2352);
    CHECK(!cdrom_reinit_ex(CDROM_READ_DATA_AREA, 1024, 2048));
    expect_reads(GDROM_DIRECT_SECTOR_MODE1);
    read_error = ETIMEDOUT;
    CHECK(cdrom_read_sectors(buffer, 150, 17) == ERR_TIMEOUT);
    CHECK(cdrom_read_sectors_ex(buffer, 150, 17, true) == ERR_TIMEOUT);
    read_error = ENOMEM;
    CHECK(!cdrom_read_sectors_async(buffer, 150, 17, 1234, callback, &token) && errno == ENOMEM);
    read_error = 0;
    CHECK(!cdrom_read_sectors_async(buffer, 150, 17, 0, callback, &token) && errno == EINVAL);
    CHECK(!bios_commands && bios_modes == 2 && !held && reads && queued);
    testing = false;
    printf("READ-ROUTING: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
