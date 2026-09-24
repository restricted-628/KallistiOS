/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdio.h>

/* This is an internal routing regression, not a public-API usage template. */
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static unsigned int checks;
static unsigned int failures;
static unsigned int direct_calls;
static unsigned int bios_calls;

static void check(bool condition, const char *label) {
    ++checks;
    if(!condition) {
        ++failures;
        printf("DISC-DEFAULTS: failed %s errno=%d\n", label, errno);
    }
}

cdrom_stream_session_t *__wrap_gdrom_direct_stream_session_start(
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t start_timeout, uint32_t idle_timeout) {
    ++direct_calls;
    check(fad == 150 && sectors == 2 && type == GDROM_DIRECT_SECTOR_MODE1
          && start_timeout == 4000 && idle_timeout == 1000,
          "direct session arguments");
    errno = ENOMSG;
    return NULL;
}

cdrom_stream_session_t *__wrap_cdrom_stream_session_start_internal(
    uint32_t fad, size_t sectors, size_t sector_size, size_t data_bytes,
    uint32_t start_timeout, uint32_t idle_timeout,
    cdrom_request_backend_t backend, gdrom_direct_sector_type_t type,
    cdrom_stream_session_finalizer_t finalizer, void *data) {
    ++bios_calls;
    check(fad == 150 && sectors == 2 && sector_size == 2048 && data_bytes == 4096
          && start_timeout == 0 && idle_timeout == 1000
          && backend == CDROM_REQUEST_BACKEND_BIOS
          && type == GDROM_DIRECT_SECTOR_MODE1 && !finalizer && !data,
          "BIOS session arguments");
    errno = ENOMSG;
    return NULL;
}

int main(void) {
    cdrom_sector_range_t *range;
    cdrom_sector_range_info_t info;

    check(fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT, "initial direct");
    check(fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS) == 0
          && fs_iso9660_get_backend() == FS_ISO9660_BACKEND_BIOS, "BIOS opt-in");
    errno = 0;
    check(fs_iso9660_set_backend((fs_iso9660_backend_t)99) < 0
          && errno == EINVAL
          && fs_iso9660_get_backend() == FS_ISO9660_BACKEND_BIOS, "invalid unchanged");

    /* Explicit BIOS filesystem selection must not change raw constructors. */
    range = cdrom_sector_range_open(150, 2);
    check(range && cdrom_sector_range_get_info(range, &info) == 0
          && info.backend == CDROM_REQUEST_BACKEND_DIRECT, "default range");
    check(range && cdrom_sector_range_close(range) == 0, "close direct range");
    range = cdrom_bios_sector_range_open(150, 2);
    check(range && cdrom_sector_range_get_info(range, &info) == 0
          && info.backend == CDROM_REQUEST_BACKEND_BIOS, "BIOS range");
    check(range && cdrom_sector_range_close(range) == 0, "close BIOS range");

    errno = 0;
    check(!cdrom_stream_session_start(150, 2, 4000, 1000) && errno == ENOMSG
          && direct_calls == 1 && bios_calls == 0, "direct session dispatch");
    errno = 0;
    check(!cdrom_bios_stream_session_start(150, 2, 0, 1000) && errno == ENOMSG
          && direct_calls == 1 && bios_calls == 1, "BIOS session dispatch");

    check(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) == 0
          && fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT, "select direct");
    check(fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS) == 0, "select BIOS before reset");
    fs_iso9660_shutdown();
    fs_iso9660_init();
    check(fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT, "reset direct");

    printf("DISC-DEFAULTS: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
