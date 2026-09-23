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
static cd_toc_t toc;
static unsigned int direct_status_calls, direct_toc_calls, bios_status_calls;
static bool high_density;
static int bios_status_error;

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

int __wrap_gdrom_direct_get_status(
    gdrom_direct_status_t *out, uint32_t timeout, gdrom_direct_result_t *result) {
    ++direct_status_calls;
    CHECK(out && timeout == 10000 && !result);
    /* A diagnostic payload can precede CHECK; don't publish it on failure. */
    out->status = CD_STATUS_PAUSED;
    out->disc_type = CD_GDROM;
    errno = direct_error;
    return direct_error ? -1 : 0;
}

int __wrap_gdrom_direct_read_toc(
    cd_toc_t *out, bool density, uint32_t timeout, gdrom_direct_result_t *result) {
    ++direct_toc_calls;
    CHECK(out == &toc && density == high_density && timeout == 10000 && result);
    result->sense_valid = sense_valid;
    result->sense = sense;
    if(direct_error) {
        errno = direct_error;
        return -1;
    }
    out->first = 0x41010000;
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
    ++bios_calls;
    CHECK(testing && held && params);
    if(cmd == CD_CMD_GETTOC2) {
        cd_cmd_toc_params_t *t = params;
        CHECK(t->buffer == &toc
              && t->area == (high_density ? CD_AREA_HIGH : CD_AREA_LOW));
        t->buffer->first = 0x41020000;
    }
    else {
        cd_cmd_getscd_params_t *q = params;
        CHECK(cmd == CD_CMD_GETSCD && q->which == CD_SUB_Q_CHANNEL
              && q->buflen == 14);
        fill_subcode(q->buffer);
    }
    return 77;
}

int __wrap_syscall_gdrom_check_drive(cd_check_drive_status_t *out) {
    ++bios_status_calls;
    CHECK(testing && held && out);
    out->status = CD_STATUS_STANDBY;
    out->disc_type = CD_CDROM_XA;
    errno = bios_status_error;
    return bios_status_error ? -1 : 0;
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

    /* Generic metadata ignores BIOS filesystem selection; explicit BIOS
       metadata ignores direct filesystem selection. */
    {
        int drive = 99, disc = 99;
        direct_error = 0;
        sense_valid = false;
        CHECK(cdrom_get_status(&drive, &disc) == 0
              && drive == CD_STATUS_PAUSED && disc == CD_GDROM);
        CHECK(cdrom_get_status(NULL, &disc) == 0 && disc == CD_GDROM);
        CHECK(cdrom_get_status(&drive, NULL) == 0 && drive == CD_STATUS_PAUSED);
        CHECK(cdrom_get_status(NULL, NULL) == 0);
        direct_error = EIO;
        CHECK(cdrom_get_status(&drive, &disc) == -1 && errno == EIO
              && drive == -1 && disc == -1);
        direct_error = EPERM;
        CHECK(cdrom_get_status(&drive, &disc) == -1 && errno == EPERM
              && drive == -1 && disc == -1);

        direct_error = 0;
        CHECK(cdrom_read_toc(&toc, false) == ERR_OK && toc.first == 0x41010000);
        high_density = true;
        CHECK(cdrom_read_toc(&toc, true) == ERR_OK && toc.first == 0x41010000);
        for(size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            direct_error = errors[i].error;
            CHECK(cdrom_read_toc(&toc, true) == errors[i].result
                  && errno == errors[i].error);
        }
        sense_valid = true;
        sense.key = CDROM_SENSE_NOT_READY;
        sense.asc = 0x3a;
        CHECK(cdrom_read_toc(&toc, true) == ERR_NO_DISC && errno == EIO);
        sense.key = CDROM_SENSE_UNIT_ATTENTION;
        CHECK(cdrom_read_toc(&toc, true) == ERR_DISC_CHG && errno == EIO);
        CHECK(direct_status_calls == 6 && direct_toc_calls == 11
              && bios_status_calls == 0 && bios_calls == 5);

        CHECK(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) == 0);
        CHECK(cdrom_bios_get_status(&drive, &disc) == 0
              && drive == CD_STATUS_STANDBY && disc == CD_CDROM_XA);
        CHECK(cdrom_bios_get_status(NULL, &disc) == 0 && disc == CD_CDROM_XA);
        CHECK(cdrom_bios_get_status(&drive, NULL) == 0 && drive == CD_STATUS_STANDBY);
        CHECK(cdrom_bios_get_status(NULL, NULL) == 0);
        bios_status_error = EIO;
        CHECK(cdrom_bios_get_status(&drive, &disc) == -1 && errno == EIO
              && drive == -1 && disc == -1);
        high_density = false;
        CHECK(cdrom_bios_read_toc(&toc, false) == ERR_OK && toc.first == 0x41020000);
        high_density = true;
        CHECK(cdrom_bios_read_toc(&toc, true) == ERR_OK && toc.first == 0x41020000);
        CHECK(direct_status_calls == 6 && direct_toc_calls == 11
              && bios_status_calls == 5 && bios_calls == 7 && !held);
    }
    testing = false;
    printf("DISC-CONVENIENCE: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
