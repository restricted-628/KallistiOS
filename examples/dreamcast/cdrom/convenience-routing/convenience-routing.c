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
static unsigned int control_calls;
static cd_cmd_code_t expected_control;
static bool control_test;
static _Alignas(32) uint8_t subcode[100];
static cd_sub_type_t subcode_type = CD_SUB_Q_CHANNEL;
static uint32_t play_start = 2, play_end = 4, play_loops = 3;
static int play_mode = CDDA_TRACKS;
static bool stream_test;
static cd_cmd_chk_t stream_response = CD_CMD_STREAMING;
static unsigned int stream_callbacks, stream_transfers, stream_callback_sets;

static void bios_stream_callback(void *data);

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

static int control_result(cd_cmd_code_t command, uint32_t timeout,
                          gdrom_direct_result_t *result) {
    ++control_calls;
    CHECK(control_test && command == expected_control
          && timeout == 10000 && result);
    result->sense_valid = sense_valid;
    result->sense = sense;
    errno = direct_error;
    return direct_error ? -1 : 0;
}

int __wrap_gdrom_direct_get_subcode(void *buffer, size_t size,
    cd_sub_type_t which, uint32_t timeout, gdrom_direct_result_t *result) {
    CHECK(buffer == subcode && size == sizeof(subcode) && which == subcode_type);
    return control_result(CD_CMD_GETSCD, timeout, result);
}

int __wrap_gdrom_direct_cdda_play(uint32_t start, uint32_t end,
    uint32_t loops, int mode, uint32_t timeout, gdrom_direct_result_t *result) {
    CHECK(start == play_start && end == play_end && mode == play_mode
          && loops == (play_loops > 15 ? 15 : play_loops));
    return control_result(mode == CDDA_TRACKS ? CD_CMD_PLAY_TRACKS
                                             : CD_CMD_PLAY_SECTORS,
                          timeout, result);
}

int __wrap_gdrom_direct_cdda_pause(uint32_t timeout, gdrom_direct_result_t *result) {
    return control_result(CD_CMD_PAUSE, timeout, result);
}

int __wrap_gdrom_direct_cdda_resume(uint32_t timeout, gdrom_direct_result_t *result) {
    return control_result(CD_CMD_RELEASE, timeout, result);
}

int __wrap_gdrom_direct_cdda_stop(uint32_t timeout, gdrom_direct_result_t *result) {
    return control_result(CD_CMD_STOP, timeout, result);
}

static int get_subcode(void) {
    return cdrom_get_subcode(subcode, sizeof(subcode), subcode_type);
}

static int play(void) {
    return cdrom_cdda_play(play_start, play_end, play_loops, play_mode);
}

static int bios_subcode(void) {
    return cdrom_bios_get_subcode(subcode, sizeof(subcode), subcode_type);
}

static int bios_play(void) {
    return cdrom_bios_cdda_play(play_start, play_end, play_loops, play_mode);
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
    if(stream_test) {
        const int *p = params;
        CHECK(testing && held && cmd == expected_control && p
              && p[0] == 150 && p[1] == 2);
        return 77;
    }
    if(control_test) {
        CHECK(testing && held && cmd == expected_control);
        if(cmd == CD_CMD_GETSCD) {
            cd_cmd_getscd_params_t *q = params;
            CHECK(q && q->buffer == subcode && q->buflen == sizeof(subcode)
                  && q->which == subcode_type);
        }
        else if(cmd == CD_CMD_PLAY_TRACKS || cmd == CD_CMD_PLAY_SECTORS) {
            cd_cmd_play_params_t *p = params;
            CHECK(p && p->start == play_start && p->end == play_end
                  && p->repeat == (play_loops > 15 ? 15 : play_loops));
        }
        else
            CHECK(!params);
        return 77;
    }
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
    return stream_test ? stream_response : CD_CMD_COMPLETED;
}

static void bios_stream_callback(void *data) {
    CHECK(stream_test && data == &user_data);
    ++stream_callbacks;
}

void __wrap_syscall_gdrom_pio_callback(uintptr_t cb, void *data) {
    CHECK(stream_test);
    CHECK((cb == (uintptr_t)bios_stream_callback && data == &user_data)
          || (!cb && !data));
    ++stream_callback_sets;
}

int __wrap_syscall_gdrom_pio_transfer(gdc_cmd_hnd_t hnd,
                                     const cd_transfer_params_t *params) {
    CHECK(stream_test && held && hnd == 77 && params
          && params->addr == subcode && params->size == 32);
    ++stream_transfers;
    return 0;
}

int __wrap_syscall_gdrom_pio_check(gdc_cmd_hnd_t hnd, size_t *size) {
    CHECK(stream_test && hnd == 77 && size);
    *size = 0;
    return 0;
}

int __wrap_syscall_gdrom_dma_check(gdc_cmd_hnd_t hnd, size_t *size) {
    CHECK(stream_test && hnd == 77 && size);
    *size = 4096;
    return 1;
}

int __wrap_syscall_gdrom_dma_transfer(gdc_cmd_hnd_t hnd,
                                     const cd_transfer_params_t *params) {
    CHECK(stream_test && held && hnd == 77 && params
          && (uintptr_t)params->addr == ((uintptr_t)subcode & MEM_AREA_CACHE_MASK)
          && params->size == 32);
    ++stream_transfers;
    return -1; /* Exercise unlock/error cleanup without a real DMA IRQ. */
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
    {
        static const struct {
            cd_cmd_code_t command;
            int (*direct)(void);
            int (*bios)(void);
        } controls[] = {
            { CD_CMD_GETSCD, get_subcode, bios_subcode },
            { CD_CMD_PLAY_TRACKS, play, bios_play },
            { CD_CMD_PAUSE, cdrom_cdda_pause, cdrom_bios_cdda_pause },
            { CD_CMD_RELEASE, cdrom_cdda_resume, cdrom_bios_cdda_resume },
            { CD_CMD_STOP, cdrom_spin_down, cdrom_bios_spin_down },
        };
        control_test = true;
        CHECK(fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS) == 0);
        for(size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
            unsigned int before = bios_calls;
            expected_control = controls[i].command;
            sense_valid = false;
            direct_error = 0;
            CHECK(controls[i].direct() == ERR_OK);
            for(size_t j = 0; j < sizeof(errors) / sizeof(errors[0]); ++j) {
                direct_error = errors[j].error;
                CHECK(controls[i].direct() == errors[j].result
                      && errno == errors[j].error);
            }
            sense_valid = true;
            sense.key = CDROM_SENSE_NOT_READY;
            sense.asc = 0x3a;
            CHECK(controls[i].direct() == ERR_NO_DISC && errno == EIO);
            sense.key = CDROM_SENSE_UNIT_ATTENTION;
            CHECK(controls[i].direct() == ERR_DISC_CHG && errno == EIO);
            CHECK(bios_calls == before); /* Never fall back on failure. */
        }
        CHECK(control_calls == 50);
        CHECK(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) == 0);
        for(size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
            unsigned int before = bios_calls;
            expected_control = controls[i].command;
            CHECK(controls[i].bios() == ERR_OK && !held);
            CHECK(bios_calls == before + 1 && control_calls == 50);
        }
        direct_error = 0;
        sense_valid = false;
        expected_control = CD_CMD_PLAY_SECTORS;
        play_mode = CDDA_SECTORS;
        play_start = 150;
        play_end = 450;
        play_loops = UINT32_MAX;
        CHECK(play() == ERR_OK && bios_play() == ERR_OK);
        play_mode = -1;
        direct_error = EINVAL; /* Invalid mode reaches direct validation. */
        CHECK(play() == ERR_SYS && errno == EINVAL);
        {
            unsigned int before = bios_calls;
            CHECK(bios_play() == ERR_OK && bios_calls == before);
        }
        direct_error = 0;
        expected_control = CD_CMD_GETSCD;
        for(int which = CD_SUB_Q_ALL; which <= CD_SUB_TRACK_ISRC; ++which) {
            subcode_type = (cd_sub_type_t)which;
            CHECK(get_subcode() == ERR_OK && bios_subcode() == ERR_OK);
        }
        CHECK(!held);
    }
    {
        size_t remaining = 99;
        unsigned int before = control_calls;
        control_test = false;
        stream_test = true;
        /* /cd still selects direct; only these explicit calls use firmware. */
        expected_control = CD_CMD_PIOREAD_STREAM;
        CHECK(cdrom_bios_stream_request(subcode, 32, true) == ERR_NO_ACTIVE);
        CHECK(cdrom_bios_stream_progress(&remaining) == 0 && remaining == 0);
        CHECK(cdrom_bios_stream_start(150, 2, false) == ERR_OK && !held);
        cdrom_bios_stream_set_callback(bios_stream_callback, &user_data);
        CHECK(cdrom_bios_stream_request(subcode + 1, 32, true) == ERR_SYS
              && !held && stream_transfers == 0);
        CHECK(cdrom_bios_stream_request(subcode, 32, true) == ERR_OK
              && !held && stream_transfers == 1 && stream_callbacks == 1);
        CHECK(cdrom_bios_stream_progress(&remaining) == 0 && remaining == 0);
        stream_response = CD_CMD_COMPLETED;
        CHECK(cdrom_bios_stream_stop(false) == ERR_OK && !held
              && stream_callback_sets == 2);
        CHECK(cdrom_bios_stream_stop(false) == ERR_OK);

        expected_control = CD_CMD_DMAREAD_STREAM;
        stream_response = CD_CMD_STREAMING;
        CHECK(cdrom_bios_stream_start(150, 2, true) == ERR_OK && !held);
        CHECK(cdrom_bios_stream_progress(&remaining) == 1 && remaining == 4096);
        CHECK(cdrom_bios_stream_request(subcode, 32, false) == ERR_SYS
              && !held && stream_transfers == 2);
        stream_response = CD_CMD_COMPLETED;
        CHECK(cdrom_bios_stream_stop(false) == ERR_OK && !held);
        CHECK(control_calls == before); /* Never enter direct control APIs. */
        stream_test = false;
    }
    testing = false;
    printf("DISC-CONVENIENCE: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
