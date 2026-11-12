/* Host signature fixture only; not installed and not a hardware simulator. */
#ifndef ADX_TEST_CDROM_H
#define ADX_TEST_CDROM_H
#include <stddef.h>
#include <stdint.h>
typedef struct cdrom_sector_range cdrom_sector_range_t;
typedef struct cdrom_request cdrom_request_t;
typedef enum cdrom_request_state {
    CDROM_REQUEST_QUEUED, CDROM_REQUEST_RUNNING, CDROM_REQUEST_COMPLETE,
    CDROM_REQUEST_CANCELLED, CDROM_REQUEST_ERROR, CDROM_REQUEST_TIMED_OUT
} cdrom_request_state_t;
typedef enum cdrom_request_backend {
    CDROM_REQUEST_BACKEND_BIOS, CDROM_REQUEST_BACKEND_DIRECT
} cdrom_request_backend_t;
typedef struct cdrom_sector_range_info {
    cdrom_request_backend_t backend;
    uint32_t start_fad;
    size_t sector_count, position;
} cdrom_sector_range_info_t;
typedef struct cdrom_request_status {
    cdrom_request_backend_t backend;
    cdrom_request_state_t state;
    int error;
    size_t data_bytes, completed_bytes;
} cdrom_request_status_t;
typedef void (*cdrom_request_callback_t)(cdrom_request_t *, const cdrom_request_status_t *, void *);
int cdrom_sector_range_get_info(cdrom_sector_range_t *, cdrom_sector_range_info_t *);
int64_t cdrom_sector_range_seek(cdrom_sector_range_t *, int64_t, int);
cdrom_request_t *cdrom_sector_range_read_async(cdrom_sector_range_t *, void *, size_t, uint32_t, cdrom_request_callback_t, void *);
int cdrom_request_get_status(const cdrom_request_t *, cdrom_request_status_t *);
int cdrom_request_destroy(cdrom_request_t *);
int cdrom_request_cancel(cdrom_request_t *);
#endif
