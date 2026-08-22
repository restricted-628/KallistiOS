/* KallistiOS ##version##

   cdrom.c

   Copyright (C) 2000 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2014 Donald Haase
   Copyright (C) 2023, 2024, 2025 Ruslan Rostovtsev
   Copyright (C) 2024 Andy Barajas
   Copyright (C) 2026 Joseph Black

 */
#include <assert.h>

#include <dc/asic.h>
#include <dc/cdrom.h>
#include <dc/g1ata.h>
#include <dc/memory.h>
#include <dc/syscalls.h>
#include <dc/vblank.h>

#include <kos/cache.h>
#include <kos/irq.h>
#include <kos/timer.h>
#include <kos/thread.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/dbglog.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "cdrom_request.h"
#include "g1_bus.h"
#include "gdrom_direct_internal.h"

/*

This module contains low-level primitives for accessing the CD-Rom (I
refer to it as a CD-Rom and not a GD-Rom, because this code will not
access the GD area, by design). Whenever a file is accessed and a new
disc is inserted, it reads the TOC for the disc in the drive and
gets everything situated. After that it will read raw sectors from
the data track on a standard DC bootable CDR (one audio track plus
one data track in xa1 format).

Initial information/algorithms in this file are thanks to
Marcus Comstedt. Thanks to Maiwe for the verbose command names and
also for the CDDA playback routines.

*/

struct cmd_req_data {
    cd_cmd_code_t cmd;
    void *data;
};

struct cmd_transfer_data {
    gdc_cmd_hnd_t hnd;
    size_t size;
};

/* Command handling */
static gdc_cmd_hnd_t cmd_hnd = 0;
static cd_cmd_chk_t cmd_response = CD_CMD_NOT_FOUND;
static cd_cmd_chk_status_t cmd_status = { 0 };

/* DMA and IRQ handling */
static bool dma_in_progress = false;
static bool dma_blocking = false;
static bool dma_auto_unlock = false;
static semaphore_t *dma_request_done = NULL;
static bool dma_request_stream = false;
static size_t dma_request_size = 0;
static semaphore_t dma_done = SEM_INITIALIZER(0);
static g1_bus_dma_client_t dma_irq_client = G1_BUS_DMA_CLIENT_INVALID;
static int vblank_hnd = -1;

/* Streaming */
static bool stream_enabled = false;
static bool stream_dma = false;
static cdrom_stream_callback_t stream_cb = NULL;
static void *stream_cb_param = NULL;

/* Initialization */
static bool inited = false;
static int cur_sector_size = 2048;

size_t cdrom_sector_size_internal(void) {
    return (size_t)cur_sector_size;
}

#define CDROM_BOOT_DISC_ID_ADDRESS    ((const uint8_t *)0x8c008000u)
#define CDROM_CURRENT_DISC_ID_ADDRESS ((const uint8_t *)0x8c008100u)
#define CDROM_DISC_SEQUENCE_OFFSET    43u
#define CDROM_DISC_SEQUENCE_SIZE      5u
#define CDROM_PRODUCT_ID_OFFSET       64u
#define CDROM_PRODUCT_ID_SIZE         10u
#define CDROM_RECOGNIZE_CADENCE_MS    20u

static int g1_bus_failure_result(void) {
    return errno == EIO ? ERR_HARDWARE : ERR_SYS;
}

static int parse_disc_sequence(const uint8_t *field,
                               uint32_t *disc_number,
                               uint32_t *disc_count) {
    uint32_t first = 0;
    uint32_t second = 0;
    size_t offset = 0;

    if(field[offset] < '0' || field[offset] > '9')
        goto malformed;

    do {
        uint32_t digit = (uint32_t)(field[offset] - '0');

        if(first > (UINT32_MAX - digit) / 10u)
            goto malformed;
        first = first * 10u + digit;
        ++offset;
    } while(offset < CDROM_DISC_SEQUENCE_SIZE
            && field[offset] >= '0' && field[offset] <= '9');

    if(offset >= CDROM_DISC_SEQUENCE_SIZE || field[offset++] != '/')
        goto malformed;
    if(offset >= CDROM_DISC_SEQUENCE_SIZE
            || field[offset] < '0' || field[offset] > '9')
        goto malformed;

    do {
        uint32_t digit = (uint32_t)(field[offset] - '0');

        if(second > (UINT32_MAX - digit) / 10u)
            goto malformed;
        second = second * 10u + digit;
        ++offset;
    } while(offset < CDROM_DISC_SEQUENCE_SIZE
            && field[offset] >= '0' && field[offset] <= '9');

    if(offset >= CDROM_DISC_SEQUENCE_SIZE || field[offset] != ' ')
        goto malformed;

    *disc_number = first;
    *disc_count = second;
    return 0;

malformed:
    errno = EPROTO;
    return -1;
}

static int get_disc_id(const uint8_t *record, cdrom_disc_id_t *id) {
    cdrom_disc_id_t parsed;

    if(!id) {
        errno = EINVAL;
        return -1;
    }

    /* The BootROM updates the current record outside normal cached CPU
       stores. Discard any old P1 cache line before parsing either record. */
    dcache_inval_range((uintptr_t)record, 96u);
    memset(&parsed, 0, sizeof(parsed));

    if(parse_disc_sequence(record + CDROM_DISC_SEQUENCE_OFFSET,
                           &parsed.disc_number, &parsed.disc_count) < 0)
        return -1;

    memcpy(parsed.product_id, record + CDROM_PRODUCT_ID_OFFSET,
           CDROM_PRODUCT_ID_SIZE);
    parsed.product_id[CDROM_PRODUCT_ID_SIZE] = '\0';
    *id = parsed;
    errno = 0;
    return 0;
}

int cdrom_media_recognize(uint32_t timeout) {
    uint64_t deadline;
    int result;

    if(!timeout) {
        errno = EINVAL;
        return -1;
    }

    if(g1_bus_lock() < 0)
        return -1;

    deadline = timer_ms_gettime64() + timeout;

    for(;;) {
        /* The firmware contract requires a whole-cache purge immediately
           before selector 2 of the BootROM system vector. Preserve that
           load-bearing ordering. */
        dcache_purge_all();
        result = syscall_system_disc_check();

        if(result >= 0) {
            g1_bus_unlock();
            errno = 0;
            return result ? 1 : 0;
        }

        uint64_t now = timer_ms_gettime64();
        uint64_t remaining;

        if(now >= deadline) {
            g1_bus_unlock();
            errno = ETIMEDOUT;
            return -1;
        }

        remaining = deadline - now;
        if(remaining < CDROM_RECOGNIZE_CADENCE_MS) {
            thd_sleep((unsigned int)remaining);
            g1_bus_unlock();
            errno = ETIMEDOUT;
            return -1;
        }

        /* Recognition is advanced at most once per VSync. A fixed 20 ms
           interval is safe for both 60 Hz and 50 Hz output. */
        thd_sleep(CDROM_RECOGNIZE_CADENCE_MS);
    }
}

int cdrom_get_boot_disc_id(cdrom_disc_id_t *id) {
    return get_disc_id(CDROM_BOOT_DISC_ID_ADDRESS, id);
}

int cdrom_get_current_disc_id(cdrom_disc_id_t *id) {
    return get_disc_id(CDROM_CURRENT_DISC_ID_ADDRESS, id);
}

int cdrom_decode_sense(const cd_cmd_chk_status_t *detail,
                       cdrom_sense_t *sense) {
    uint32_t additional;

    if(!detail || !sense) {
        errno = EINVAL;
        return -1;
    }

    additional = (uint32_t)detail->err2;
    sense->key = (cdrom_sense_key_t)detail->err1;
    sense->asc = additional & 0xff;
    sense->ascq = (additional >> 8) & 0xff;
    return 0;
}

int cdrom_sense_to_result(const cdrom_sense_t *sense) {
    if(!sense) {
        errno = EINVAL;
        return ERR_SYS;
    }

    switch(sense->key) {
        case CDROM_SENSE_RECOVERED_ERROR:
            return ERR_RECOVERED;
        case CDROM_SENSE_NOT_READY:
            return sense->asc == 0x3a ? ERR_NO_DISC : ERR_NOT_READY;
        case CDROM_SENSE_MEDIUM_ERROR:
            return ERR_MEDIA;
        case CDROM_SENSE_HARDWARE_ERROR:
            return ERR_HARDWARE;
        case CDROM_SENSE_ILLEGAL_REQUEST:
            return ERR_ILLEGAL_REQUEST;
        case CDROM_SENSE_UNIT_ATTENTION:
            return ERR_DISC_CHG;
        case CDROM_SENSE_DATA_PROTECT:
            return ERR_PROTECT;
        case CDROM_SENSE_ABORTED_COMMAND:
            return ERR_ABORTED;
        case CDROM_SENSE_NOT_READABLE:
            return ERR_NOT_READABLE;
        case CDROM_SENSE_G1_SEMAPHORE:
            return ERR_BUSY;
        case CDROM_SENSE_NONE:
        default:
            return ERR_SYS;
    }
}

int cdrom_status_to_result(cd_cmd_chk_t response,
                           const cd_cmd_chk_status_t *detail) {
    cdrom_sense_t sense;

    switch(response) {
        case CD_CMD_COMPLETED:
        case CD_CMD_STREAMING:
            return ERR_OK;
        case CD_CMD_NOT_FOUND:
            return ERR_NO_ACTIVE;
        case CD_CMD_PROCESSING:
        case CD_CMD_BUSY:
            return ERR_BUSY;
        case CD_CMD_FAILED:
            break;
        default:
            return ERR_SYS;
    }

    if(!detail || cdrom_decode_sense(detail, &sense) < 0)
        return ERR_SYS;

    return cdrom_sense_to_result(&sense);
}

int cdrom_result_to_errno(int result) {
    switch(result) {
        case ERR_OK:
            return 0;
        case ERR_NO_DISC:
            /* Newlib only exposes ENOMEDIUM with non-portable Linux errno
               extensions enabled. Preserve KOS's established mapping while
               leaving the more precise result and ASC available to callers. */
            return ENODEV;
        case ERR_DISC_CHG:
            return ESTALE;
        case ERR_ABORTED:
            return ECANCELED;
        case ERR_NO_ACTIVE:
            return ENOENT;
        case ERR_TIMEOUT:
            return ETIMEDOUT;
        case ERR_NOT_READY:
            return EAGAIN;
        case ERR_ILLEGAL_REQUEST:
            return EINVAL;
        case ERR_PROTECT:
            return EACCES;
        case ERR_NOT_READABLE:
            return ENOTSUP;
        case ERR_BUSY:
            return EBUSY;
        case ERR_RECOVERED:
        case ERR_MEDIA:
        case ERR_HARDWARE:
        case ERR_SYS:
        default:
            return EIO;
    }
}

/* Drive/media monitoring. The monitor only tries to acquire G1, so it never
   queues behind a long read or staged stream. Application callbacks run while
   media_event_mutex is held; add/remove reject callback context, making a
   successful removal a lifetime barrier for callback data. */
#define CDROM_MEDIA_POLL_MS 100
#define CDROM_MEDIA_DIRECT_TIMEOUT_MS 50

typedef struct cdrom_media_handler {
    TAILQ_ENTRY(cdrom_media_handler) entry;
    int handle;
    cdrom_media_event_callback_t callback;
    void *data;
} cdrom_media_handler_t;

static TAILQ_HEAD(cdrom_media_handler_list, cdrom_media_handler)
    media_handlers = TAILQ_HEAD_INITIALIZER(media_handlers);
static mutex_t media_event_mutex = MUTEX_INITIALIZER;
static semaphore_t media_monitor_wake = SEM_INITIALIZER(0);
/* Creation and handler membership are serialized by media_event_mutex. The
   unlocked reads only avoid unnecessary work or detect callback context;
   `volatile` would not make concurrent subsystem teardown safe. */
static kthread_t *media_monitor_thread;
static volatile bool media_monitor_quit;
static volatile bool media_monitor_direct;
static cdrom_drive_state_t media_cached_state;
static bool media_cached_state_valid;
static int media_next_handle = 1;
static bool media_pending_event_valid;
static cdrom_media_event_type_t media_pending_event;
static cdrom_request_backend_t media_pending_backend;

static unsigned int media_event_priority(cdrom_media_event_type_t type) {
    switch(type) {
        case CDROM_MEDIA_EVENT_FATAL:
            return 4;
        case CDROM_MEDIA_EVENT_REMOVED:
        case CDROM_MEDIA_EVENT_CHANGED:
            return 3;
        case CDROM_MEDIA_EVENT_ERROR:
            return 2;
        case CDROM_MEDIA_EVENT_RECOVERED:
        case CDROM_MEDIA_EVENT_INSERTED:
        default:
            return 1;
    }
}

void cdrom_media_monitor_report_result(
        int result, cdrom_request_backend_t backend) {
    cdrom_media_event_type_t type;
    irq_mask_t irq;

    if(!media_monitor_thread || media_monitor_quit)
        return;

    if(g1_bus_is_faulted()) {
        type = CDROM_MEDIA_EVENT_FATAL;
    }
    else {
        switch(result) {
            case ERR_NO_DISC:
                type = CDROM_MEDIA_EVENT_REMOVED;
                break;
            case ERR_DISC_CHG:
                type = CDROM_MEDIA_EVENT_CHANGED;
                break;
            case ERR_MEDIA:
            case ERR_HARDWARE:
            case ERR_NOT_READABLE:
                type = CDROM_MEDIA_EVENT_ERROR;
                break;
            default:
                return;
        }
    }

    irq = irq_disable();
    if(!media_pending_event_valid
            || media_event_priority(type)
                >= media_event_priority(media_pending_event)) {
        media_pending_event = type;
        media_pending_backend = backend;
        media_pending_event_valid = true;
    }
    irq_restore(irq);
    sem_signal(&media_monitor_wake);
}

/* These helpers let the request worker participate in the existing GD
   DMA IRQ state machine without exposing that state as public API. The request
   worker holds G1 ownership for the complete lifetime of the command. */
int cdrom_dma_request_begin(gdc_cmd_hnd_t handle, semaphore_t *done,
                            size_t transfer_size, bool stream_transfer) {
    irq_mask_t irq = irq_disable();

    if(dma_in_progress) {
        irq_restore(irq);
        return -1;
    }

    cmd_hnd = handle;
    cmd_response = CD_CMD_BUSY;
    cmd_status = (cd_cmd_chk_status_t){ 0 };
    dma_in_progress = true;
    dma_blocking = false;
    dma_auto_unlock = false;
    dma_request_done = done;
    dma_request_stream = stream_transfer;
    dma_request_size = transfer_size;

    irq_restore(irq);
    return 0;
}

bool cdrom_dma_request_snapshot(gdc_cmd_hnd_t handle,
                                cd_cmd_chk_t *response,
                                cd_cmd_chk_status_t *status) {
    irq_mask_t irq = irq_disable();
    bool available = cmd_hnd == handle;

    if(available) {
        *response = cmd_response;
        *status = cmd_status;

        if(dma_request_stream && dma_in_progress) {
            size_t remaining = dma_request_size;

            if(syscall_gdrom_dma_check(handle, &remaining) >= 0) {
                if(remaining > dma_request_size)
                    remaining = dma_request_size;
                status->size = dma_request_size - remaining;
            }
        }
    }

    irq_restore(irq);
    return available;
}

bool cdrom_dma_request_active(gdc_cmd_hnd_t handle) {
    irq_mask_t irq = irq_disable();
    bool active = dma_in_progress && cmd_hnd == handle;

    irq_restore(irq);
    return active;
}

void cdrom_dma_request_end(gdc_cmd_hnd_t handle) {
    irq_mask_t irq = irq_disable();

    if(cmd_hnd == handle) {
        dma_in_progress = false;
        dma_blocking = false;
        dma_auto_unlock = false;
        dma_request_done = NULL;
        dma_request_stream = false;
        dma_request_size = 0;
        cmd_hnd = 0;
    }

    irq_restore(irq);
}

/* Called by the request worker with G1 ownership held. A legacy stream
   does not retain that semaphore between transfers, so it must be retired
   before a queued staged session can claim the command server. */
int cdrom_stream_request_claim(void) {
    cd_cmd_chk_status_t status = { 0 };
    cd_cmd_chk_t response;
    gdc_cmd_hnd_t handle;
    uint64_t deadline;

    if(!stream_enabled || cmd_hnd <= 0)
        return ERR_OK;

    handle = cmd_hnd;
    deadline = timer_ms_gettime64() + 1000;
    {
        irq_mask_t irq = irq_disable();

        syscall_gdrom_abort_command(handle);
        irq_restore(irq);
    }

    do {
        irq_mask_t irq = irq_disable();

        syscall_gdrom_exec_server();
        response = syscall_gdrom_check_command(handle, &status);
        irq_restore(irq);

        if(response == CD_CMD_NOT_FOUND || response == CD_CMD_COMPLETED)
            break;

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    if(response != CD_CMD_NOT_FOUND && response != CD_CMD_COMPLETED) {
        irq_mask_t irq = irq_disable();

        syscall_gdrom_reset();
        syscall_gdrom_init();
        irq_restore(irq);
    }

    cmd_hnd = 0;
    stream_enabled = false;
    if(stream_cb)
        cdrom_stream_set_callback(NULL, NULL);

    return response == CD_CMD_NOT_FOUND || response == CD_CMD_COMPLETED
        ? ERR_OK : ERR_TIMEOUT;
}

bool cdrom_stream_sector_size_matches(size_t sector_size) {
    return cur_sector_size > 0 && (size_t)cur_sector_size == sector_size;
}

/* Shortcut to cdrom_reinit_ex. Typically this is the only thing changed. */
int cdrom_set_sector_size(int size) {
    return cdrom_reinit_ex(CDROM_READ_DEFAULT, -1, size);
}

static int cdrom_poll(void *d, uint32_t timeout, int (*cb)(void *)) {
    int ret;

    ret = thd_poll(cb, d, timeout);

    return ret == 0 ? ERR_TIMEOUT : ret;
}

static gdc_cmd_hnd_t cdrom_submit_cmd(void *d) {
    struct cmd_req_data *req = d;
    gdc_cmd_hnd_t ret;

    ret = syscall_gdrom_send_command(req->cmd, req->data);

    syscall_gdrom_exec_server();

    return ret;
}

static inline gdc_cmd_hnd_t cdrom_req_cmd(cd_cmd_code_t cmd, void *param) {
    struct cmd_req_data req = { cmd, param };

    assert(cmd > 0 && cmd < CD_CMD_MAX);

    /* Submit the command, retry if needed for 10ms */
    return (gdc_cmd_hnd_t)cdrom_poll(&req, 10, (int (*)(void *))cdrom_submit_cmd);
}

static int cdrom_check_ready(void *d) {
    syscall_gdrom_exec_server();

    cmd_response = syscall_gdrom_check_command(*(int *)d, &cmd_status);
    if(cmd_response <= CD_CMD_FAILED)
        return ERR_SYS;

    return cmd_response != CD_CMD_BUSY;
}

static int cdrom_check_cmd_done(void *d) {
    syscall_gdrom_exec_server();

    cmd_response = syscall_gdrom_check_command(*(int *)d, &cmd_status);
    if(cmd_response <= CD_CMD_FAILED)
        return ERR_SYS;

    return cmd_response != CD_CMD_BUSY && cmd_response != CD_CMD_PROCESSING;
}

static int cdrom_check_abort_done(void *d) {
    syscall_gdrom_exec_server();

    cmd_response = syscall_gdrom_check_command(*(gdc_cmd_hnd_t *)d, &cmd_status);
    if(cmd_response <= CD_CMD_FAILED)
        return ERR_SYS;

    return cmd_response == CD_CMD_NOT_FOUND || cmd_response == CD_CMD_COMPLETED;
}

static int cdrom_check_abort_streaming(void *d) {
    syscall_gdrom_exec_server();

    cmd_response = syscall_gdrom_check_command(*(gdc_cmd_hnd_t *)d, &cmd_status);
    if(cmd_response <= CD_CMD_FAILED)
        return ERR_SYS;

    return cmd_response == CD_CMD_NOT_FOUND || cmd_response == CD_CMD_COMPLETED
        || cmd_response == CD_CMD_STREAMING;
}

static int cdrom_check_transfer(void *d) {
    struct cmd_transfer_data *data = d;

    syscall_gdrom_exec_server();

    cmd_response = syscall_gdrom_check_command(data->hnd, &cmd_status);
    if(cmd_response <= CD_CMD_FAILED)
        return ERR_SYS;

    if(cmd_response == CD_CMD_NOT_FOUND || cmd_response == CD_CMD_COMPLETED)
        return ERR_NO_ACTIVE;

    return cdrom_stream_progress(&data->size) == 0;
}

/* Command execution sequence */
int cdrom_exec_cmd(cd_cmd_code_t cmd, void *param) {
    return cdrom_exec_cmd_timed(cmd, param, 0);
}

int cdrom_exec_cmd_timed(cd_cmd_code_t cmd, void *param, uint32_t timeout) {
    int result;

    if(g1_bus_lock() < 0)
        return g1_bus_failure_result();

    cmd_hnd = cdrom_req_cmd(cmd, param);

    if(cmd_hnd <= 0) {
        g1_bus_unlock();
        return ERR_SYS;
    }

    /* Start the process of executing the command. */
    if(cdrom_poll(&cmd_hnd, timeout, cdrom_check_cmd_done) == ERR_TIMEOUT) {
        /* cdrom_abort_cmd() acquires G1 itself when no DMA owns it. */
        g1_bus_unlock();
        cdrom_abort_cmd(1000, true);
        return ERR_TIMEOUT;
    }

    if(cmd_response != CD_CMD_STREAMING) {
        cmd_hnd = 0;
    }

    result = cdrom_status_to_result(cmd_response, &cmd_status);
    g1_bus_unlock();
    return result;
}

int cdrom_abort_cmd(uint32_t timeout, bool abort_dma) {
    int rv = ERR_OK;
    irq_mask_t old = irq_disable();

    if(cmd_hnd <= 0) {
        irq_restore(old);
        return ERR_NO_ACTIVE;
    }

    if(abort_dma && dma_in_progress) {
        dma_in_progress = false;
        dma_blocking = false;
        dma_auto_unlock = false;
        if(dma_request_done) {
            sem_signal(dma_request_done);
            dma_request_done = NULL;
        }
        dma_request_stream = false;
        dma_request_size = 0;
        /* G1 ATA mutex already locked */
    }
    else {
        if(g1_bus_lock() < 0) {
            irq_restore(old);
            return g1_bus_failure_result();
        }
    }

    irq_restore(old);
    syscall_gdrom_abort_command(cmd_hnd);

    if(cdrom_poll(&cmd_hnd, timeout, cdrom_check_abort_done) == ERR_TIMEOUT) {
        dbglog(DBG_ERROR, "cdrom_abort_cmd: Timeout exceeded, resetting.\n");
        rv = ERR_TIMEOUT;
        syscall_gdrom_reset();
        syscall_gdrom_init();
    }

    cmd_hnd = 0;
    stream_enabled = false;

    if(stream_cb) {
        cdrom_stream_set_callback(0, NULL);
    }

    g1_bus_unlock();
    return rv;
}

static int check_drive_status(cd_check_drive_status_t *status,
                              bool try_only) {
    int rv;

    if(try_only) {
        if(g1_bus_trylock() < 0)
            return -1;
    }
    else if(g1_bus_lock() < 0) {
        return -1;
    }

    rv = syscall_gdrom_check_drive(status);
    g1_bus_unlock();
    return rv;
}

void cdrom_media_monitor_use_direct(bool direct) {
    irq_mask_t irq = irq_disable();
    bool changed = media_monitor_direct != direct;

    media_monitor_direct = direct;
    irq_restore(irq);
    if(changed)
        sem_signal(&media_monitor_wake);
}

static int sample_media_status(cd_check_drive_status_t *sample,
                               cdrom_request_backend_t *backend) {
    gdrom_direct_status_t direct_status;
    gdrom_direct_result_t transport;
    bool direct;
    int rv;

    direct = media_monitor_direct;
    *backend = direct ? CDROM_REQUEST_BACKEND_DIRECT
                      : CDROM_REQUEST_BACKEND_BIOS;

    if(g1_bus_trylock() < 0)
        return errno == EWOULDBLOCK ? 1 : -1;

    if(direct) {
        rv = gdrom_direct_get_status_locked(
            &direct_status, CDROM_MEDIA_DIRECT_TIMEOUT_MS, &transport);
        if(rv == 0) {
            sample->status = direct_status.status;
            sample->disc_type = direct_status.disc_type;
        }
    }
    else {
        rv = syscall_gdrom_check_drive(sample);
    }

    /* A failed direct recovery marks G1 faulted and releases waiters itself.
       Calling unlock in that state would report another error and is not the
       ownership release mechanism. */
    if(!g1_bus_is_faulted())
        g1_bus_unlock();
    return rv;
}

/* Return the status of the drive as two integers (see constants) */
int cdrom_get_status(int *status, int *disc_type) {
    cd_check_drive_status_t stat;
    int rv;

    /* We might be called in an interrupt to check for ISO cache
       flushing, so make sure we're not interrupting something
       already in progress. */
    rv = check_drive_status(&stat, false);

    if(rv >= 0) {
        rv = ERR_OK;

        if(status != NULL)
            *status = stat.status;

        if(disc_type != NULL)
            *disc_type = stat.disc_type;
    }
    else {
        if(status != NULL)
            *status = -1;

        if(disc_type != NULL)
            *disc_type = -1;
    }

    return rv;
}

static bool media_present(cd_stat_t status) {
    return status != CD_STATUS_READ_FAIL && status != CD_STATUS_OPEN
        && status != CD_STATUS_NO_DISC && status != CD_STATUS_FATAL;
}

static bool classify_media_event(const cdrom_drive_state_t *previous,
                                 const cdrom_drive_state_t *current,
                                 cdrom_media_event_type_t *type) {
    bool previous_present = media_present(previous->status);
    bool current_present = media_present(current->status);

    if(current->status == CD_STATUS_FATAL
            && previous->status != CD_STATUS_FATAL) {
        *type = CDROM_MEDIA_EVENT_FATAL;
        return true;
    }

    if(current->status == CD_STATUS_ERROR
            && previous->status != CD_STATUS_ERROR) {
        *type = CDROM_MEDIA_EVENT_ERROR;
        return true;
    }

    if((previous->status == CD_STATUS_ERROR
            || previous->status == CD_STATUS_FATAL)
            && current->status != CD_STATUS_ERROR
            && current->status != CD_STATUS_FATAL) {
        *type = CDROM_MEDIA_EVENT_RECOVERED;
        return true;
    }

    if(previous_present && !current_present) {
        *type = CDROM_MEDIA_EVENT_REMOVED;
        return true;
    }

    if(!previous_present && current_present) {
        *type = CDROM_MEDIA_EVENT_INSERTED;
        return true;
    }

    if(previous_present && current_present
            && previous->disc_type != current->disc_type) {
        *type = CDROM_MEDIA_EVENT_CHANGED;
        return true;
    }

    return false;
}

static void dispatch_media_event(const cdrom_media_event_t *event) {
    cdrom_media_handler_t *handler;

    /* Handler removal takes the same mutex, so returning from remove is also
       a guarantee that an in-flight callback no longer references its data. */
    mutex_lock(&media_event_mutex);
    TAILQ_FOREACH(handler, &media_handlers, entry)
        handler->callback(event, handler->data);
    mutex_unlock(&media_event_mutex);
}

static void publish_media_sample(const cd_check_drive_status_t *sample,
                                 cdrom_request_backend_t backend) {
    cdrom_media_event_t event;
    irq_mask_t irq;
    uint64_t timestamp = timer_ms_gettime64();
    bool previous_valid;
    bool dispatch;

    irq = irq_disable();
    previous_valid = media_cached_state_valid;
    event.previous = media_cached_state;
    event.current = (cdrom_drive_state_t) {
        .status = sample->status,
        .disc_type = sample->disc_type,
        .backend = backend,
        .sequence = previous_valid ? media_cached_state.sequence + 1 : 1,
        .timestamp = timestamp,
    };
    media_cached_state = event.current;
    media_cached_state_valid = true;
    irq_restore(irq);

    if(previous_valid) {
        dispatch = classify_media_event(
            &event.previous, &event.current, &event.type);
    }
    else if(event.current.status == CD_STATUS_FATAL) {
        event.type = CDROM_MEDIA_EVENT_FATAL;
        dispatch = true;
    }
    else if(event.current.status == CD_STATUS_ERROR) {
        event.type = CDROM_MEDIA_EVENT_ERROR;
        dispatch = true;
    }
    else {
        dispatch = false;
    }
    if(dispatch)
        dispatch_media_event(&event);
}

static bool take_pending_media_event(cdrom_media_event_type_t *type,
                                     cdrom_request_backend_t *backend) {
    irq_mask_t irq = irq_disable();
    bool pending = media_pending_event_valid;

    if(pending) {
        *type = media_pending_event;
        *backend = media_pending_backend;
        media_pending_event_valid = false;
    }
    irq_restore(irq);
    return pending;
}

static void publish_forced_media_event(cdrom_media_event_type_t type,
                                       cdrom_request_backend_t backend) {
    cdrom_media_event_t event;
    irq_mask_t irq;
    uint64_t timestamp = timer_ms_gettime64();
    bool previous_valid;

    irq = irq_disable();
    previous_valid = media_cached_state_valid;
    event.previous = media_cached_state;
    event.current = previous_valid
        ? media_cached_state : (cdrom_drive_state_t) { 0 };
    event.type = type;
    event.current.backend = backend;
    event.current.sequence = previous_valid
        ? media_cached_state.sequence + 1 : 1;
    event.current.timestamp = timestamp;
    switch(type) {
        case CDROM_MEDIA_EVENT_REMOVED:
            event.current.status = CD_STATUS_NO_DISC;
            break;
        case CDROM_MEDIA_EVENT_FATAL:
            event.current.status = CD_STATUS_FATAL;
            break;
        case CDROM_MEDIA_EVENT_ERROR:
            event.current.status = CD_STATUS_ERROR;
            break;
        case CDROM_MEDIA_EVENT_CHANGED:
            if(!previous_valid)
                event.current.status = CD_STATUS_ERROR;
            break;
        default:
            break;
    }
    media_cached_state = event.current;
    media_cached_state_valid = true;
    irq_restore(irq);

    /* Request failures force an event even before the first successful sample:
       the operation itself is sufficient evidence to invalidate ISO state. */
    dispatch_media_event(&event);
}

static void publish_failed_media_sample(cdrom_request_backend_t backend) {
    cd_check_drive_status_t sample;
    bool faulted = g1_bus_is_faulted();
    irq_mask_t irq = irq_disable();

    sample.status = faulted ? CD_STATUS_FATAL : CD_STATUS_ERROR;
    sample.disc_type = media_cached_state_valid
        ? media_cached_state.disc_type : 0;
    irq_restore(irq);
    publish_media_sample(&sample, backend);
}

static void *media_monitor_routine(void *data) {
    cd_check_drive_status_t sample;
    cdrom_request_backend_t backend;
    cdrom_media_event_type_t pending_type;
    int sample_result;

    (void)data;

    while(!media_monitor_quit) {
        while(take_pending_media_event(&pending_type, &backend))
            publish_forced_media_event(pending_type, backend);

        /* A monitor sample must never queue behind a long read or a staged
           stream. Failure to claim G1 returns a positive deferral; a real
           transport failure becomes a cached ERROR/FATAL observation. */
        sample_result = sample_media_status(&sample, &backend);
        if(!media_monitor_quit) {
            if(sample_result == 0)
                publish_media_sample(&sample, backend);
            else if(sample_result < 0)
                publish_failed_media_sample(backend);
        }

        sem_wait_timed(&media_monitor_wake, CDROM_MEDIA_POLL_MS);
    }

    return NULL;
}

static void media_monitor_reset(void) {
    irq_mask_t irq;

    media_next_handle = 1;

    irq = irq_disable();
    media_cached_state = (cdrom_drive_state_t) { 0 };
    media_cached_state_valid = false;
    media_pending_event_valid = false;
    media_monitor_quit = false;
    media_monitor_direct = false;
    irq_restore(irq);
}

static int media_monitor_start(void) {
    static const kthread_attr_t attrs = {
        .prio = PRIO_DEFAULT + 2,
        .label = "[gdrom-media]",
    };
    kthread_t *thread;

    if(!inited) {
        errno = ENODEV;
        return -1;
    }

    mutex_lock(&media_event_mutex);
    if(media_monitor_thread) {
        mutex_unlock(&media_event_mutex);
        return 0;
    }
    if(media_monitor_quit || !inited) {
        mutex_unlock(&media_event_mutex);
        errno = ENODEV;
        return -1;
    }

    thread = thd_create_ex(&attrs, media_monitor_routine, NULL);
    if(!thread) {
        mutex_unlock(&media_event_mutex);
        errno = ENOMEM;
        return -1;
    }
    media_monitor_thread = thread;
    mutex_unlock(&media_event_mutex);
    return 0;
}

static void media_monitor_shutdown(void) {
    cdrom_media_handler_t *handler;
    cdrom_media_handler_t *next;
    kthread_t *thread = media_monitor_thread;

    if(!thread)
        return;

    media_monitor_quit = true;
    sem_signal(&media_monitor_wake);
    /* This join is intentionally a lifetime barrier, not a timeout. Continuing
       teardown while a callback still uses driver code or caller-owned data
       would be unsafe; callbacks are therefore required to return promptly. */
    thd_join(thread, NULL);

    mutex_lock(&media_event_mutex);
    media_monitor_thread = NULL;
    handler = TAILQ_FIRST(&media_handlers);
    while(handler) {
        next = TAILQ_NEXT(handler, entry);
        free(handler);
        handler = next;
    }
    TAILQ_INIT(&media_handlers);
    mutex_unlock(&media_event_mutex);
}

int cdrom_get_cached_drive_state(cdrom_drive_state_t *state) {
    irq_mask_t irq;

    if(!state) {
        errno = EINVAL;
        return -1;
    }
    if(media_monitor_start() < 0)
        return -1;

    irq = irq_disable();
    if(!media_cached_state_valid) {
        irq_restore(irq);
        errno = EAGAIN;
        return -1;
    }
    *state = media_cached_state;
    irq_restore(irq);
    return 0;
}

int cdrom_media_event_handler_add(cdrom_media_event_callback_t callback,
                                  void *data) {
    cdrom_media_handler_t *handler;
    int handle;

    if(!callback) {
        errno = EINVAL;
        return -1;
    }
    if(thd_get_current() == media_monitor_thread) {
        errno = EDEADLK;
        return -1;
    }
    if(media_monitor_start() < 0)
        return -1;

    handler = malloc(sizeof(*handler));
    if(!handler) {
        errno = ENOMEM;
        return -1;
    }

    mutex_lock(&media_event_mutex);
    if(!media_monitor_thread) {
        mutex_unlock(&media_event_mutex);
        free(handler);
        errno = ENODEV;
        return -1;
    }
    handle = media_next_handle++;
    if(media_next_handle <= 0)
        media_next_handle = 1;
    handler->handle = handle;
    handler->callback = callback;
    handler->data = data;
    TAILQ_INSERT_TAIL(&media_handlers, handler, entry);
    mutex_unlock(&media_event_mutex);
    return handle;
}

int cdrom_media_event_handler_remove(int handle) {
    cdrom_media_handler_t *handler;

    if(handle <= 0) {
        errno = EINVAL;
        return -1;
    }
    if(thd_get_current() == media_monitor_thread) {
        errno = EDEADLK;
        return -1;
    }

    mutex_lock(&media_event_mutex);
    TAILQ_FOREACH(handler, &media_handlers, entry) {
        if(handler->handle == handle) {
            TAILQ_REMOVE(&media_handlers, handler, entry);
            mutex_unlock(&media_event_mutex);
            free(handler);
            return 0;
        }
    }
    mutex_unlock(&media_event_mutex);

    errno = ENOENT;
    return -1;
}

/* Wrapper for the change datatype syscall */
int cdrom_change_datatype(cd_read_sec_part_t sector_part, int track_type, int sector_size) {
    cd_check_drive_status_t status;
    cd_sec_mode_params_t params;
    int result;

    if(g1_bus_lock() < 0)
        return g1_bus_failure_result();

    /* Check if we are using default params */
    if(sector_size == 2352) {
        if(track_type == -1)
            track_type = 0;

        if(sector_part == CDROM_READ_DEFAULT)
            sector_part = CDROM_READ_WHOLE_SECTOR;
    }
    else {
        if(track_type == -1) {
            /* If not overriding cdxa, check what the drive thinks we should 
               use */
            syscall_gdrom_check_drive(&status);
            track_type = (status.disc_type == CD_CDROM_XA ? 2048 : 1024);
        }

        if(sector_part == CDROM_READ_DEFAULT)
            sector_part = CDROM_READ_DATA_AREA;

        if(sector_size == -1)
            sector_size = 2048;
    }

    params.rw = 0;                      /* 0 = set, 1 = get */
    params.sector_part = sector_part;   /* Get Data or Full Sector */
    params.track_type  = track_type;    /* CD-XA mode 1/2 */
    params.sector_size = sector_size;   /* sector size */

    cur_sector_size = sector_size;
    result = syscall_gdrom_sector_mode(&params);
    g1_bus_unlock();
    return result;
}

/* Re-init the drive, e.g., after a disc change, etc */
int cdrom_reinit(void) {
    /* By setting -1 to each parameter, they fall to the old defaults */
    return cdrom_reinit_ex(CDROM_READ_DEFAULT, -1, -1);
}

/* Enhanced cdrom_reinit, takes the place of the old 'sector_size' function */
int cdrom_reinit_ex(cd_read_sec_part_t sector_part, int cdxa, int sector_size) {
    int r;

    do {
        r = cdrom_exec_cmd_timed(CD_CMD_INIT, NULL, 10000);
    } while(r == ERR_DISC_CHG);

    if(r == ERR_NO_DISC || r == ERR_SYS || r == ERR_TIMEOUT
            || r == ERR_HARDWARE) {
        return r;
    }

    return cdrom_change_datatype(sector_part, cdxa, sector_size);
}

/* Read the table of contents */
int cdrom_read_toc(cd_toc_t *toc_buffer, bool high_density) {
    cd_cmd_toc_params_t params;

    params.area = high_density ? CD_AREA_HIGH : CD_AREA_LOW;
    params.buffer = toc_buffer;

    return cdrom_exec_cmd(CD_CMD_GETTOC2, &params);
}

static int cdrom_read_sectors_dma_irq(cd_read_params_t *params) {
    int result;

    if(g1_bus_lock() < 0)
        return g1_bus_failure_result();

    cmd_hnd = cdrom_req_cmd(CD_CMD_DMAREAD, params);

    if(cmd_hnd <= 0) {
        g1_bus_unlock();
        return ERR_SYS;
    }
    dma_in_progress = true;
    dma_blocking = true;

    /* Start the process of executing the command. */
    cdrom_poll(&cmd_hnd, 0, cdrom_check_ready);

    if(cmd_response == CD_CMD_PROCESSING) {
        /* Wait DMA is finished or command failed. */
        sem_wait(&dma_done);

        if(cmd_response != CD_CMD_FAILED) {
            /* Just to make sure the command is finished properly.
               Usually we are already done here. A Holly DMA fault is already
               terminal and must not be overwritten by BIOS polling. */
            cdrom_poll(&cmd_hnd, 0, cdrom_check_cmd_done);
        }
    }
    else {
        /* The command can complete or fails immediately,
           in this case we just countdown the semaphore if needed.
        */
        if(sem_count(&dma_done) > 0) {
            sem_wait(&dma_done);
        }
    }

    cmd_hnd = 0;

    if(cmd_response == CD_CMD_COMPLETED || cmd_response == CD_CMD_NOT_FOUND) {
        g1_bus_unlock();
        return ERR_OK;
    }

    result = cdrom_status_to_result(cmd_response, &cmd_status);
    g1_bus_unlock();
    return result;
}

/* Enhanced Sector reading: Choose mode to read in. */
int cdrom_read_sectors_ex(void *buffer, uint32_t sector, size_t cnt, bool dma) {
    cd_read_params_t params;
    uintptr_t buf_addr = ((uintptr_t)buffer);

    params.start_sec = sector;  /* Starting sector */
    params.num_sec = cnt;       /* Number of sectors */
    params.is_test = 0;         /* Enable test mode */

    if(dma) {
        if(!__builtin_is_aligned(buf_addr, 32)) {
            dbglog(DBG_ERROR, "cdrom_read_sectors_ex: Unaligned memory for DMA (32-byte).\n");
            return ERR_SYS;
        }
        /* Use the physical memory address. */
        params.buffer = (void *)(buf_addr & MEM_AREA_CACHE_MASK);

        /* Invalidate the CPU cache only for cacheable memory areas.
           Otherwise, it is assumed that either this operation is unnecessary
           (another DMA is being used) or that the caller is responsible
           for managing the CPU data cache.
        */
        if((buf_addr & MEM_AREA_P2_BASE) != MEM_AREA_P2_BASE) {
            /* Invalidate the dcache over the range of the data. */
            dcache_inval_range(buf_addr, cnt * cur_sector_size);
        }
        return cdrom_read_sectors_dma_irq(&params);
    }
    else {
        params.buffer = buffer;

        if(!__builtin_is_aligned(buf_addr, 2)) {
            dbglog(DBG_ERROR, "cdrom_read_sectors_ex: Unaligned memory for PIO (2-byte).\n");
            return ERR_SYS;
        }
        return cdrom_exec_cmd(CD_CMD_PIOREAD, &params);
    }

    return ERR_OK;
}

/* Basic old sector read */
int cdrom_read_sectors(void *buffer, uint32_t sector, size_t cnt) {
    return cdrom_read_sectors_ex(buffer, sector, cnt, false);
}

cdrom_request_t *cdrom_read_sectors_async(
    void *buffer, uint32_t sector, size_t cnt, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    return cdrom_read_sectors_async_internal(
        buffer, sector, cnt, timeout, NULL, NULL, callback, callback_data);
}

int cdrom_request_dma_segment_init(
    cdrom_request_dma_segment_t *segment, void *buffer, uint32_t sector,
    size_t sector_count, size_t data_offset, size_t data_bytes,
    bool data_direct) {
    return cdrom_request_dma_segment_init_sized(
        segment, buffer, sector, sector_count, (size_t)cur_sector_size,
        data_offset, data_bytes, data_direct);
}

int cdrom_request_dma_segment_init_sized(
    cdrom_request_dma_segment_t *segment, void *buffer, uint32_t sector,
    size_t sector_count, size_t sector_size, size_t data_offset,
    size_t data_bytes, bool data_direct) {
    uintptr_t buf_addr = (uintptr_t)buffer;
    size_t io_bytes;

    if(!segment || !buffer || !sector_count || !sector_size
            || !__builtin_is_aligned(buf_addr, 32)
            || sector_count > SIZE_MAX / sector_size) {
        errno = EINVAL;
        return -1;
    }

    io_bytes = sector_count * sector_size;
    if(data_offset > io_bytes || data_bytes > io_bytes - data_offset) {
        errno = EINVAL;
        return -1;
    }

    *segment = (cdrom_request_dma_segment_t) {
        .params = {
            .start_sec = sector,
            .num_sec = sector_count,
            .buffer = (void *)(buf_addr & MEM_AREA_CACHE_MASK),
            .is_test = 0,
        },
        .buffer = buffer,
        .io_bytes = io_bytes,
        .data_offset = data_offset,
        .data_bytes = data_bytes,
        .cacheable = (buf_addr & MEM_AREA_P2_BASE) != MEM_AREA_P2_BASE,
        .data_direct = data_direct,
    };

    return 0;
}

cdrom_request_t *cdrom_read_sectors_async_internal(
    void *buffer, uint32_t sector, size_t cnt, uint32_t timeout,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data) {
    cdrom_request_dma_segment_t segment;

    if(cdrom_request_dma_segment_init(
            &segment, buffer, sector, cnt, 0,
            cnt * (size_t)cur_sector_size, true) < 0)
        return NULL;

    return cdrom_request_submit_dma_chain(
        &segment, segment.io_bytes, segment.io_bytes, segment.io_bytes, timeout,
        NULL, NULL, finalizer, finalizer_data, callback, callback_data);
}

cdrom_request_t *cdrom_seek_async(
    uint32_t sector, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    return cdrom_seek_async_internal(sector, timeout, NULL, NULL, callback,
                                     callback_data);
}

cdrom_request_t *cdrom_seek_async_internal(
    uint32_t sector, uint32_t timeout,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data) {
    return cdrom_request_submit_internal(
        CD_CMD_SEEK, &sector, sizeof(sector), timeout, finalizer,
        finalizer_data, callback, callback_data);
}

cdrom_stream_session_t *cdrom_stream_session_start(
    uint32_t sector, size_t sector_count, uint32_t start_timeout,
    uint32_t idle_timeout) {
    size_t sector_size;

    if(cur_sector_size <= 0) {
        errno = EINVAL;
        return NULL;
    }
    sector_size = (size_t)cur_sector_size;
    if(sector_count > SIZE_MAX / sector_size) {
        errno = EINVAL;
        return NULL;
    }

    return cdrom_stream_session_start_internal(
        sector, sector_count, sector_size, sector_count * sector_size,
        start_timeout, idle_timeout, CDROM_REQUEST_BACKEND_BIOS,
        GDROM_DIRECT_SECTOR_MODE1, NULL, NULL);
}

int cdrom_stream_start(int sector, int cnt, bool dma) {
    struct {
        int sec;
        int num;
    } params;
    int rv = ERR_SYS;

    params.sec = sector;
    params.num = cnt;

    if(stream_enabled) {
        cdrom_stream_stop(false);
    }
    stream_dma = dma;

    if(stream_dma) {
        rv = cdrom_exec_cmd_timed(CD_CMD_DMAREAD_STREAM, &params, 0);
    }
    else {
        rv = cdrom_exec_cmd_timed(CD_CMD_PIOREAD_STREAM, &params, 0);
    }

    if(rv != ERR_OK) {
        stream_enabled = false;
    }
    return rv;
}

int cdrom_stream_stop(bool abort_dma) {
    if(cmd_hnd <= 0) {
        return ERR_OK;
    }
    if(abort_dma && dma_in_progress) {
        return cdrom_abort_cmd(1000, true);
    }
    if(g1_bus_lock() < 0)
        return g1_bus_failure_result();

    cdrom_poll(&cmd_hnd, 0, cdrom_check_abort_streaming);

    if(cmd_response == CD_CMD_STREAMING) {
        g1_bus_unlock();
        return cdrom_abort_cmd(1000, false);
    }

    cmd_hnd = 0;
    stream_enabled = false;
    g1_bus_unlock();

    if(stream_cb) {
        cdrom_stream_set_callback(0, NULL);
    }
    return ERR_OK;
}

int cdrom_stream_request(void *buffer, size_t size, bool block) {
    int rs;
    uintptr_t buf_addr = ((uintptr_t)buffer);
    cd_transfer_params_t params;
    struct cmd_transfer_data data;

    if(cmd_hnd <= 0) {
        return ERR_NO_ACTIVE;
    }
    if(dma_in_progress) {
        dbglog(DBG_ERROR, "cdrom_stream_request: Previous DMA request is in progress.\n");
        return ERR_SYS;
    }

    if(stream_dma) {
        if(!__builtin_is_aligned(buf_addr, 32)) {
            dbglog(DBG_ERROR, "cdrom_stream_request: Unaligned memory for DMA (32-byte).\n");
            return ERR_SYS;
        }
        /* Use the physical memory address. */
        params.addr = (void *)(buf_addr & MEM_AREA_CACHE_MASK);

        /* Invalidate the CPU cache only for cacheable memory areas.
           Otherwise, it is assumed that either this operation is unnecessary
           (another DMA is being used) or that the caller is responsible
           for managing the CPU data cache.
        */
        if((buf_addr & MEM_AREA_P2_BASE) != MEM_AREA_P2_BASE) {
            /* Invalidate the dcache over the range of the data. */
            dcache_inval_range(buf_addr, size);
        }
    }
    else {
        params.addr = buffer;

        if(!__builtin_is_aligned(buf_addr, 2)) {
            dbglog(DBG_ERROR, "cdrom_stream_request: Unaligned memory for PIO (2-byte).\n");
            return ERR_SYS;
        }
    }

    params.size = size;
    if(g1_bus_lock() < 0)
        return g1_bus_failure_result();

    if(stream_dma) {
        dma_in_progress = true;
        dma_blocking = block;
        dma_auto_unlock = !block;

        rs = syscall_gdrom_dma_transfer(cmd_hnd, &params);

        if(rs < 0) {
            dma_in_progress = false;
            dma_blocking = false;
            dma_auto_unlock = false;
            g1_bus_unlock();
            return ERR_SYS;
        }
        if(!block) {
            return ERR_OK;
        }
        sem_wait(&dma_done);
        if(cmd_response == CD_CMD_FAILED) {
            rs = cdrom_status_to_result(cmd_response, &cmd_status);
            g1_bus_unlock();
            return rs;
        }
    }
    else {
        rs = syscall_gdrom_pio_transfer(cmd_hnd, &params);
        if(rs < 0) {
            g1_bus_unlock();
            return ERR_SYS;
        }
    }

    data = (struct cmd_transfer_data){ cmd_hnd, 0 };

    if(cdrom_poll(&data, 0, cdrom_check_transfer) == ERR_NO_ACTIVE) {
        cmd_hnd = 0;
    }
    else if(!stream_dma) {
        /* Syscalls doesn't call it on last reading in PIO mode.
           Looks like a bug, fixing it. */
        if(data.size == 0 && stream_cb)
            stream_cb(stream_cb_param);
    }

    g1_bus_unlock();
    return ERR_OK;
}

int cdrom_stream_progress(size_t *size) {
    int rv = 0;
    size_t check_size = 0;

    if(cmd_hnd <= 0) {
        if(size) {
            *size = check_size;
        }
        return rv;
    }

    if(stream_dma) {
        rv = syscall_gdrom_dma_check(cmd_hnd, &check_size);
    }
    else {
        rv = syscall_gdrom_pio_check(cmd_hnd, &check_size);
    }

    if(size) {
        *size = check_size;
    }
    return rv;
}

void cdrom_stream_set_callback(cdrom_stream_callback_t callback, void *param) {
    stream_cb = callback;
    stream_cb_param = param;

    if(!stream_dma) {
        syscall_gdrom_pio_callback((uintptr_t)stream_cb, stream_cb_param);
    }
}

/* Read a piece of or all of the Q byte of the subcode of the last sector read.
   If you need the subcode from every sector, you cannot read more than one at 
   a time. */
/* XXX: Use some CD-Gs and other stuff to test if you get more than just the 
   Q byte */
int cdrom_get_subcode(void *buffer, size_t buflen, cd_sub_type_t which) {
    cd_cmd_getscd_params_t params = { .which = which, .buflen = buflen, .buffer = buffer };
    return cdrom_exec_cmd(CD_CMD_GETSCD, &params);
}

typedef struct cdda_status_request {
    cdrom_cdda_status_t *status;
    uint8_t subcode[14];
} cdda_status_request_t;

void cdrom_decode_cdda_status_internal(
        const uint8_t subcode[14], cdrom_cdda_status_t *status) {
    /* CD_SUB_Q_CHANNEL is already decoded by the BIOS: track/index and both
       24-bit frame addresses are binary. Only CD_SUB_Q_ALL contains the raw
       BCD-encoded Q channel. */
    uint32_t elapsed = ((uint32_t)subcode[7] << 16)
        | ((uint32_t)subcode[8] << 8) | subcode[9];

    status->audio_status = (cd_sub_audio_t)subcode[1];
    status->control = subcode[4] >> 4;
    status->adr = subcode[4] & 0x0f;
    status->track = subcode[5];
    status->index = subcode[6];
    status->track_elapsed_frames = elapsed;
    status->track_minutes = elapsed / (75 * 60);
    status->track_seconds = (elapsed / 75) % 60;
    status->track_frames = elapsed % 75;
    status->fad = ((uint32_t)subcode[11] << 16)
        | ((uint32_t)subcode[12] << 8) | subcode[13];
}

int cdrom_cdda_get_status(cdrom_cdda_status_t *status) {
    uint8_t subcode[14];
    int result;

    if(!status) {
        errno = EINVAL;
        return ERR_SYS;
    }

    result = cdrom_get_subcode(subcode, sizeof(subcode), CD_SUB_Q_CHANNEL);
    if(result == ERR_OK)
        cdrom_decode_cdda_status_internal(subcode, status);
    return result;
}

static void cdda_status_finalize(
    cdrom_request_t *request, const cdrom_request_status_t *request_status,
    void *data) {
    cdda_status_request_t *cdda = data;

    (void)request;

    if(request_status->state == CDROM_REQUEST_COMPLETE)
        cdrom_decode_cdda_status_internal(cdda->subcode, cdda->status);
    free(cdda);
}

cdrom_request_t *cdrom_cdda_get_status_async(
    cdrom_cdda_status_t *status, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    cdda_status_request_t *cdda;
    cd_cmd_getscd_params_t params;
    cdrom_request_t *request;

    if(!status) {
        errno = EINVAL;
        return NULL;
    }

    cdda = calloc(1, sizeof(*cdda));
    if(!cdda) {
        errno = ENOMEM;
        return NULL;
    }

    cdda->status = status;
    params = (cd_cmd_getscd_params_t) {
        .which = CD_SUB_Q_CHANNEL,
        .buflen = sizeof(cdda->subcode),
        .buffer = cdda->subcode,
    };
    request = cdrom_request_submit_internal(
        CD_CMD_GETSCD, &params, sizeof(params), timeout,
        cdda_status_finalize, cdda, callback, callback_data);
    if(!request)
        free(cdda);
    return request;
}

/* Locate the LBA sector of the data track; use after reading TOC */
uint32_t cdrom_locate_data_track(cd_toc_t *toc) {
    int i, first, last;

    first = TOC_TRACK(toc->first);
    last = TOC_TRACK(toc->last);

    if(first < 1 || last > 99 || first > last)
        return 0;

    /* Find the last track which as a CTRL of 4 */
    for(i = last; i >= first; i--) {
        if(TOC_CTRL(toc->entry[i - 1]) == 4)
            return TOC_LBA(toc->entry[i - 1]);
    }

    return 0;
}

/* Play CDDA tracks
   start  -- track to play from
   end    -- track to play to
   repeat -- number of times to repeat (0-15, 15=infinite)
   mode   -- CDDA_TRACKS or CDDA_SECTORS
 */
int cdrom_cdda_play(uint32_t start, uint32_t end, uint32_t repeat, int mode) {
    cd_cmd_play_params_t params;

    /* Limit to 0-15 */
    if(repeat > 15)
        repeat = 15;

    params.start = start;
    params.end = end;
    params.repeat = repeat;

    if(mode == CDDA_TRACKS)
        return cdrom_exec_cmd(CD_CMD_PLAY_TRACKS, &params);
    else if(mode == CDDA_SECTORS)
        return cdrom_exec_cmd(CD_CMD_PLAY_SECTORS, &params);
    else
        return ERR_OK;
}

/* Pause CDDA audio playback */
int cdrom_cdda_pause(void) {
    return cdrom_exec_cmd(CD_CMD_PAUSE, NULL);
}

/* Resume CDDA audio playback */
int cdrom_cdda_resume(void) {
    return cdrom_exec_cmd(CD_CMD_RELEASE, NULL);
}

/* Spin down the CD */
int cdrom_spin_down(void) {
    return cdrom_exec_cmd(CD_CMD_STOP, NULL);
}

static void cdrom_vblank(uint32_t evt, void *data) {
    (void)evt;
    (void)data;

    if(dma_in_progress) {
        bool transfer_done = false;

        syscall_gdrom_exec_server();
        cmd_response = syscall_gdrom_check_command(cmd_hnd, &cmd_status);

        if(dma_request_stream && cmd_response == CD_CMD_STREAMING) {
            size_t remaining = dma_request_size;
            int progress = syscall_gdrom_dma_check(cmd_hnd, &remaining);

            if(remaining > dma_request_size)
                remaining = dma_request_size;
            cmd_status.size = dma_request_size - remaining;
            transfer_done = progress == 0;
        }
        else {
            transfer_done = cmd_response != CD_CMD_PROCESSING
                && cmd_response != CD_CMD_BUSY
                && cmd_response != CD_CMD_STREAMING;
        }

        if(transfer_done) {
            dma_in_progress = false;

            if(dma_request_done) {
                semaphore_t *done = dma_request_done;

                dma_request_done = NULL;
                sem_signal(done);
                thd_schedule(true);
            }
            else if(dma_blocking) {
                dma_blocking = false;
                sem_signal(&dma_done);
                thd_schedule(true);
            }
        }
    }
}

static bool g1_dma_irq_hnd(uint32_t code, void *data) {
    (void)data;

    if(!dma_in_progress)
        return false;

    dma_in_progress = false;

    if(code != ASIC_EVT_GD_DMA) {
        /* Holly DMA faults are terminal and are not BIOS command-server
           completion. In particular, do not touch the task-file here. */
        g1_bus_dma_disable();
        cmd_response = CD_CMD_FAILED;
        cmd_status.err1 = CDROM_SENSE_HARDWARE_ERROR;
        cmd_status.err2 = 0;
        cmd_status.ata = ATA_STAT_INTERNAL;
        dbglog(DBG_ERROR,
               "g1_dma_irq_hnd: GD DMA hardware error event %04lx\n",
               (unsigned long)code);

        if(dma_request_done) {
            semaphore_t *done = dma_request_done;

            dma_request_done = NULL;
            sem_signal(done);
            thd_schedule(true);
        }
        else if(dma_blocking) {
            dma_blocking = false;
            sem_signal(&dma_done);
            thd_schedule(true);
        }
        else if(dma_auto_unlock) {
            g1_bus_unlock();
            dma_auto_unlock = false;
        }

        return true;
    }

    syscall_gdrom_exec_server();
    cmd_response = syscall_gdrom_check_command(cmd_hnd, &cmd_status);

    if(dma_request_stream)
        cmd_status.size = dma_request_size;

    if(dma_request_done) {
        semaphore_t *done = dma_request_done;

        dma_request_done = NULL;
        sem_signal(done);
        thd_schedule(true);
    }
    else if(dma_blocking) {
        dma_blocking = false;
        sem_signal(&dma_done);
        thd_schedule(true);
    }
    else if(dma_auto_unlock) {
        g1_bus_unlock();
        dma_auto_unlock = false;
    }
    if(stream_enabled)
        syscall_gdrom_dma_callback((uintptr_t)stream_cb, stream_cb_param);

    return true;
}

/*
    Unlocks G1 ATA DMA access to all memory on the root bus, not just system memory.
    Patches syscall region where the DMA protection register is set,
    ensuring it allows broader memory access, and updates the register accordingly.
 */
static void unlock_dma_memory(void) {
    uint32_t i, patched = 0;
    size_t flush_size;
    volatile uint32_t *prot_reg = (uint32_t *)(G1_ATA_DMA_PROTECTION | MEM_AREA_P2_BASE);
    uintptr_t patch_addr[2] = {0x0c001c20, 0x0c0023fc};

    for(i = 0; i < sizeof(patch_addr) / sizeof(uintptr_t); ++i) {
        if(*(uint32_t *)(patch_addr[i] | MEM_AREA_P2_BASE) == (uint32_t)G1_ATA_DMA_UNLOCK_SYSMEM) {
            *(uint32_t *)(patch_addr[i] | MEM_AREA_P2_BASE) = G1_ATA_DMA_UNLOCK_ALLMEM;
            ++patched;
        }
    }
    if(patched) {
        flush_size = (patch_addr[1] - patch_addr[0]) + CACHE_L1_ICACHE_LINESIZE;
        flush_size &= ~(CACHE_L1_ICACHE_LINESIZE - 1);
        icache_sync_range(patch_addr[0] | MEM_AREA_P1_BASE, flush_size);
    }
    *prot_reg = G1_ATA_DMA_UNLOCK_ALLMEM;
}

/* Initialize: assume no threading issues */
void cdrom_init(void) {
    uint32_t p;
    volatile uint32_t *react = (uint32_t *)(G1_ATA_BUS_PROTECTION | MEM_AREA_P2_BASE);
    volatile uint32_t *state = (uint32_t *)(G1_ATA_BUS_PROTECTION_STATUS | MEM_AREA_P2_BASE);
    volatile uint32_t *bios = (uint32_t *)MEM_AREA_P2_BASE;

    if(inited) {
        return;
    }

    if(g1_bus_lock() < 0) {
        dbglog(DBG_ERROR,
               "cdrom_init: G1 is unavailable; GD-ROM remains disabled\n");
        return;
    }

    /*
        First, check the protection status to determine if it's necessary 
        to pass check the entire BIOS again.
    */
    if (*state != G1_ATA_BUS_PROTECTION_STATUS_PASSED) {
        /* Reactivate drive: send the BIOS size and then read each
        word across the bus so the controller can verify it.
        If first bytes are 0xe6ff instead of usual 0xe3ff, then
        hardware is fitted with custom BIOS using magic bootstrap
        which can and must pass controller verification with only
        the first 1024 bytes */
        if((*(uint16_t *)MEM_AREA_P2_BASE) == 0xe6ff) {
            *react = 0x3ff;
            for(p = 0; p < 0x400 / sizeof(bios[0]); p++) {
                (void)bios[p];
            }
        } else {
            *react = 0x1fffff;
            for(p = 0; p < 0x200000 / sizeof(bios[0]); p++) {
                (void)bios[p];
            }
        }
    }

    syscall_gdrom_init();

    unlock_dma_memory();
    g1_bus_unlock();

    dma_irq_client = g1_bus_dma_client_register(g1_dma_irq_hnd, NULL);
    if(dma_irq_client == G1_BUS_DMA_CLIENT_INVALID) {
        dbglog(DBG_ERROR, "cdrom_init: couldn't register G1 DMA client\n");
        return;
    }

    vblank_hnd = vblank_handler_add(cdrom_vblank, NULL);
    if(vblank_hnd < 0) {
        dbglog(DBG_ERROR, "cdrom_init: couldn't register vblank handler\n");
        g1_bus_dma_client_unregister(dma_irq_client);
        dma_irq_client = G1_BUS_DMA_CLIENT_INVALID;
        return;
    }

    inited = true;

    cdrom_reinit();

    (void)cdrom_request_system_init();
    /* Cached-state sampling is optional. Its thread is created by the first
       cached-state query or event-handler registration. */
    media_monitor_reset();
}

void cdrom_shutdown(void) {

    if(!inited) {
        return;
    }

    /* Stop event callbacks before request teardown, so callbacks cannot race
       the transition to an unavailable request engine. The monitor only tries
       to claim G1 and therefore cannot be stuck behind an active request. */
    media_monitor_shutdown();

    /* This must complete before fs_iso9660_shutdown(). ISO9660 request
       finalizers retain file handles and use the ISO9660 file-handle mutex.
       arch_auto_shutdown() preserves that ordering while leaving unrelated
       hardware available until all filesystems have finished cleanup. */
    cdrom_request_system_shutdown();

    vblank_handler_remove(vblank_hnd);

    if(dma_irq_client != G1_BUS_DMA_CLIENT_INVALID) {
        g1_bus_dma_client_unregister(dma_irq_client);
        dma_irq_client = G1_BUS_DMA_CLIENT_INVALID;
    }
    inited = false;
}
