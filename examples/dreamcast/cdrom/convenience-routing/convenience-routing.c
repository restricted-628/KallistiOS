/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <dc/fs_iso9660.h>
#include <dc/syscalls.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Link-time transport spies: no disc command is issued by this probe. */
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

static unsigned int checks, failures, direct_calls, bios_calls;
static bool testing, held, reject;
static int token, user_data, direct_error;
static bool sense_valid;
static cdrom_sense_t sense;
static cdrom_cdda_status_t status;

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("DISC-CONVENIENCE: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

static cdrom_request_t *handle(void) {
    /* Identity only; this sentinel is never dereferenced as a request. */
    return (cdrom_request_t *)&token;
}

static void callback(cdrom_request_t *request,
                     const cdrom_request_status_t *result, void *data) {
    (void)request;
    (void)result;
    (void)data;
    CHECK(false); /* Submission spies must not invoke callbacks. */
}

cdrom_request_t *__wrap_gdrom_direct_seek_async(
    uint32_t fad, uint32_t timeout, gdrom_direct_result_t *result,
    cdrom_request_callback_t cb, void *data) {
    ++direct_calls;
    CHECK(fad == 321 && timeout == 1234 && !result
          && cb == callback && data == &user_data);
    errno = ENOMEM;
    return reject ? NULL : handle();
}

cdrom_request_t *__wrap_gdrom_direct_cdda_get_status_async(
    cdrom_cdda_status_t *out, uint32_t timeout, gdrom_direct_result_t *result,
    cdrom_request_callback_t cb, void *data) {
    ++direct_calls;
    CHECK(out == &status && timeout == 2345 && !result
          && cb == callback && data == &user_data);
    errno = ENOMEM;
    return reject ? NULL : handle();
}

int __wrap_gdrom_direct_cdda_get_status(
    cdrom_cdda_status_t *out, uint32_t timeout, gdrom_direct_result_t *result) {
    ++direct_calls;
    CHECK(out == &status && timeout == 10000 && result);
    result->sense_valid = sense_valid;
    result->sense = sense;
    if(direct_error) {
        errno = direct_error;
        return -1;
    }
    out->fad = 12345;
    return 0;
}

static void fill_subcode(void *data) {
    const uint8_t q[14] = { 0, 0x11, 0, 0, 0x41, 2, 1,
                           0, 1, 2, 0, 0, 3, 4 };
    memcpy(data, q, sizeof(q));
}

cdrom_request_t *__wrap_cdrom_request_submit_internal(
    cd_cmd_code_t cmd, const void *params, size_t size, uint32_t timeout,
    cdrom_request_finalizer_t finalize, void *finalize_data,
    cdrom_request_callback_t cb, void *data) {
    ++bios_calls;
    CHECK(timeout == 0 && cb == callback && data == &user_data);
    if(cmd == CD_CMD_SEEK) {
        CHECK(size == sizeof(uint32_t) && *(const uint32_t *)params == 321
              && !finalize && !finalize_data);
    }
    else {
        const cd_cmd_getscd_params_t *q = params;
        CHECK(cmd == CD_CMD_GETSCD && size == sizeof(*q)
              && q->which == CD_SUB_Q_CHANNEL && q->buflen == 14
              && finalize && finalize_data);
        if(!reject) {
            cdrom_request_status_t done = { .state = CDROM_REQUEST_COMPLETE };
            fill_subcode(q->buffer);
            finalize(handle(), &done, finalize_data);
        }
    }
    errno = ENOMEM;
    return reject ? NULL : handle();
}

int __real_g1_bus_lock(void);
int __real_g1_bus_unlock(void);

int __wrap_g1_bus_lock(void) {
    if(!testing)
        return __real_g1_bus_lock();
    CHECK(!held);
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

gdc_cmd_hnd_t __wrap_syscall_gdrom_send_command(cd_cmd_code_t cmd, void *params) {
    cd_cmd_getscd_params_t *q = params;
    ++bios_calls;
    CHECK(testing && held && cmd == CD_CMD_GETSCD && q
          && q->which == CD_SUB_Q_CHANNEL && q->buflen == 14);
    fill_subcode(q->buffer);
    return 77;
}

void __wrap_syscall_gdrom_exec_server(void) {
    CHECK(testing && held);
}

cd_cmd_chk_t __wrap_syscall_gdrom_check_command(
    gdc_cmd_hnd_t hnd, cd_cmd_chk_status_t *out) {
    CHECK(testing && held && hnd == 77);
    memset(out, 0, sizeof(*out));
    return CD_CMD_COMPLETED;
}

static void check_bios_status(void) {
    CHECK(status.audio_status == 0x11 && status.control == 4 && status.adr == 1
          && status.track == 2 && status.index == 1
          && status.track_elapsed_frames == 258 && status.fad == 772);
}

int main(void) {
    static const struct { int error, result; } errors[] = {
        { ECANCELED, ERR_ABORTED }, { ETIMEDOUT, ERR_TIMEOUT },
        { EBUSY, ERR_BUSY }, { EAGAIN, ERR_BUSY },
        { ENOENT, ERR_ILLEGAL_REQUEST }, { EINVAL, ERR_SYS },
        { EIO, ERR_SYS }
    };
    testing = true;
    CHECK(fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS) == 0);
    CHECK(cdrom_seek_async(321, 1234, callback, &user_data) == handle());
    CHECK(cdrom_cdda_get_status_async(&status, 2345, callback, &user_data)
          == handle());
    CHECK(cdrom_cdda_get_status(&status) == ERR_OK && status.fad == 12345);
    reject = true;
    CHECK(!cdrom_seek_async(321, 1234, callback, &user_data) && errno == ENOMEM);
    CHECK(!cdrom_cdda_get_status_async(&status, 2345, callback, &user_data)
          && errno == ENOMEM);
    for(size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        direct_error = errors[i].error;
        CHECK(cdrom_cdda_get_status(&status) == errors[i].result
              && errno == errors[i].error && status.fad == 12345);
    }
    sense_valid = true;
    sense.key = CDROM_SENSE_NOT_READY;
    sense.asc = 0x3a;
    CHECK(cdrom_cdda_get_status(&status) == ERR_NO_DISC && errno == EIO);
    sense.key = CDROM_SENSE_UNIT_ATTENTION;
    CHECK(cdrom_cdda_get_status(&status) == ERR_DISC_CHG && errno == EIO);
    CHECK(direct_calls == 14 && bios_calls == 0); /* No failure fallback. */

    reject = false;
    CHECK(cdrom_bios_seek_async(321, 0, callback, &user_data) == handle());
    CHECK(cdrom_bios_cdda_get_status_async(&status, 0, callback, &user_data)
          == handle());
    check_bios_status();
    memset(&status, 0, sizeof(status));
    CHECK(cdrom_bios_cdda_get_status(&status) == ERR_OK && !held);
    check_bios_status();
    reject = true;
    CHECK(!cdrom_bios_cdda_get_status_async(&status, 0, callback, &user_data)
          && errno == ENOMEM);
    CHECK(!cdrom_bios_seek_async(321, 0, callback, &user_data) && errno == ENOMEM);
    CHECK(!cdrom_bios_cdda_get_status_async(NULL, 0, callback, &user_data)
          && errno == EINVAL);
    CHECK(cdrom_bios_cdda_get_status(NULL) == ERR_SYS && errno == EINVAL);
    CHECK(direct_calls == 14 && bios_calls == 5);
    testing = false;
    printf("DISC-CONVENIENCE: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
