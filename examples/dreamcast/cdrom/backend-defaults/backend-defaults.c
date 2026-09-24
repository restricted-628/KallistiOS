/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* This is an internal routing regression, not a public-API usage template. */
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static unsigned int checks;
static unsigned int failures;
static unsigned int direct_calls;
static unsigned int bios_calls;
static kthread_t *density_owner;
static unsigned density_tocs;
static int density_disc;

static void check(bool condition, const char *label) {
    ++checks;
    if(!condition) {
        ++failures;
        printf("DISC-DEFAULTS: failed %s errno=%d\n", label, errno);
    }
}

int __real_gdrom_direct_probe(gdrom_direct_probe_result_t *, uint32_t);
int __real_gdrom_direct_read_toc(cd_toc_t *, bool, uint32_t, gdrom_direct_result_t *);
int __real_cdrom_bios_reinit(void);
int __real_cdrom_bios_get_status(int *, int *);
int __real_cdrom_bios_read_toc(cd_toc_t *, bool);
int __real_cdrom_media_recognize(uint32_t);

static bool density_test(void) {
    return density_owner && thd_get_current() == density_owner;
}

int __wrap_gdrom_direct_probe(gdrom_direct_probe_result_t *out, uint32_t timeout) {
    if(!density_test()) return __real_gdrom_direct_probe(out, timeout);
    memset(out, 0, sizeof(*out));
    out->result = ERR_OK;
    out->status.status = CD_STATUS_PAUSED;
    out->status.disc_type = density_disc;
    return 0;
}

int __wrap_gdrom_direct_read_toc(cd_toc_t *toc, bool high, uint32_t timeout,
                               gdrom_direct_result_t *result) {
    if(!density_test()) return __real_gdrom_direct_read_toc(toc, high, timeout, result);
    ++density_tocs;
    check(!high, "direct mount always low density");
    /* Stop before sector I/O: this is a mount-selection test, not a fake disc. */
    errno = EIO;
    return -1;
}

int __wrap_cdrom_bios_reinit(void) {
    return density_test() ? ERR_OK : __real_cdrom_bios_reinit();
}

int __wrap_cdrom_bios_get_status(int *status, int *disc) {
    if(!density_test()) return __real_cdrom_bios_get_status(status, disc);
    if(status) *status = CD_STATUS_PAUSED;
    if(disc) *disc = density_disc;
    return ERR_OK;
}

int __wrap_cdrom_bios_read_toc(cd_toc_t *toc, bool high) {
    if(!density_test()) return __real_cdrom_bios_read_toc(toc, high);
    ++density_tocs;
    check(!high, "BIOS mount always low density");
    return ERR_SYS;
}

int __wrap_cdrom_media_recognize(uint32_t timeout) {
    return density_test() ? 0 : __real_cdrom_media_recognize(timeout);
}

static void check_mount_density(void) {
    const int media[] = { CD_CDROM, CD_CDROM_XA, CD_GDROM };
    const fs_iso9660_backend_t backends[] = {
        FS_ISO9660_BACKEND_DIRECT, FS_ISO9660_BACKEND_BIOS,
    };
    density_owner = thd_get_current();
    for(unsigned b = 0; b < 2; ++b) {
        for(unsigned m = 0; m < sizeof(media) / sizeof(media[0]); ++m) {
            /* Backend choice locks at the first mount attempt, even failure. */
            fs_iso9660_shutdown();
            fs_iso9660_init();
            density_disc = media[m];
            density_tocs = 0;
            check(fs_iso9660_set_backend(backends[b]) == 0, "density backend");
            file_t fd = fs_open("/cd/density-probe", O_RDONLY);
            check(fd == FILEHND_INVALID && density_tocs == 1,
                  "one low-density TOC attempt; no fallback or sector read");
            if(fd != FILEHND_INVALID) fs_close(fd);
        }
    }
    density_owner = NULL;
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

    check_mount_density();
    printf("DISC-DEFAULTS: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
