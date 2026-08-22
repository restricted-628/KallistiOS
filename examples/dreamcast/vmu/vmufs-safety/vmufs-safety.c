/* KallistiOS ##version##

   VMU filesystem transaction validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/fs_vmu.h>
#include <dc/maple.h>
#include <dc/vmu_pkg.h>
#include <dc/vmufs.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define LOW_LEVEL_NAME "KOSVMTXN"
#define VFS_NAME       "KOSVFSTST"
#define CANCEL_NAME    "KOSVMCANCEL"
#define DEFRAG_A_NAME  "KOSVMDFRAGA"
#define DEFRAG_B_NAME  "KOSVMDFRAGB"
#define DEFRAG_C_NAME  "KOSVMDFRAGC"
#define PASS_NAME      "KOSVMPASS"
#define FAIL_NAME      "KOSVMFAIL"

typedef struct callback_state {
    size_t calls;
    size_t last_completed;
    bool monotonic;
    bool live_progress;
    bool terminal;
} callback_state_t;

static bool request_terminal(vmufs_request_state_t state) {
    return state == VMUFS_REQUEST_COMPLETE
        || state == VMUFS_REQUEST_CANCELLED
        || state == VMUFS_REQUEST_ERROR;
}

static void request_callback(vmufs_request_t *request,
                             const vmufs_request_status_t *status,
                             void *data) {
    callback_state_t *state = data;

    (void)request;
    if(status->completed_blocks < state->last_completed)
        state->monotonic = false;
    state->last_completed = status->completed_blocks;
    state->calls++;

    if(!request_terminal(status->state) && status->completed_blocks > 0)
        state->live_progress = true;
    if(request_terminal(status->state))
        state->terminal = true;
}

static int finish_successful_request(vmufs_request_t *request,
                                     callback_state_t *callback,
                                     size_t expected_total,
                                     size_t expected_data,
                                     bool require_live_progress) {
    vmufs_request_status_t status;

    if(vmufs_request_wait(request, 15000, &status) < 0 ||
       status.state != VMUFS_REQUEST_COMPLETE || status.result != 0 ||
       status.error != 0 || !status.committed ||
       status.completed_blocks != expected_total ||
       status.total_blocks != expected_total ||
       status.data_blocks_completed != expected_data ||
       status.data_blocks != expected_data ||
       vmufs_request_wait_callback(request, 1000) < 0 ||
       !callback->monotonic || !callback->terminal || callback->calls == 0 ||
       (require_live_progress && !callback->live_progress)) {
        vmufs_request_cancel(request);
        vmufs_request_wait(request, 0, NULL);
        vmufs_request_wait_callback(request, 0);
        vmufs_request_destroy(request);
        return -1;
    }

    return vmufs_request_destroy(request);
}

static int finish_maintenance_request(vmufs_request_t *request,
                                      size_t expected_total,
                                      size_t expected_data) {
    vmufs_request_status_t status;

    if(!request || vmufs_request_wait(request, 30000, &status) < 0 ||
       status.state != VMUFS_REQUEST_COMPLETE || status.result != 0 ||
       status.error != 0 || !status.committed ||
       status.completed_blocks != expected_total ||
       status.total_blocks != expected_total ||
       status.data_blocks_completed != expected_data ||
       status.data_blocks != expected_data) {
        if(request) {
            vmufs_request_cancel(request);
            vmufs_request_wait(request, 0, NULL);
            vmufs_request_wait_callback(request, 0);
            vmufs_request_destroy(request);
        }
        return -1;
    }

    return vmufs_request_destroy(request);
}

static maple_device_t *first_card(void) {
    for(int port = 0; port < MAPLE_PORT_COUNT; ++port) {
        for(int unit = 0; unit < MAPLE_UNIT_COUNT; ++unit) {
            maple_device_t *dev = maple_enum_dev(port, unit);

            if(dev && (dev->info.functions & MAPLE_FUNC_MEMCARD))
                return dev;
        }
    }

    return NULL;
}

static int validate_card(maple_device_t *dev) {
    vmu_root_t root;
    vmu_dir_t *dir = NULL;
    uint16_t *fat = NULL;
    vmufs_validation_t result;
    size_t dir_bytes, fat_bytes;
    int rv = -1;

    vmufs_mutex_lock();

    if(vmufs_root_read(dev, &root) < 0 ||
       vmufs_root_validate(&root, 256) < 0)
        goto out;

    dir_bytes = vmufs_dir_blocks(&root);
    fat_bytes = vmufs_fat_blocks(&root);
    dir = malloc(dir_bytes);
    fat = malloc(fat_bytes);
    if(!dir || !fat)
        goto out;

    if(vmufs_dir_read(dev, &root, dir) < 0 ||
       vmufs_fat_read(dev, &root, fat) < 0)
        goto out;

    rv = vmufs_validate(&root, 256, fat, fat_bytes / sizeof(*fat),
                        dir, dir_bytes / sizeof(*dir), &result);
    printf("files=%zu free=%zu exec-free=%zu orphans=%zu errors=%zu\n",
           result.file_count, result.free_blocks,
           result.executable_free_blocks, result.orphan_blocks,
           result.error_count);

out:
    free(fat);
    free(dir);
    vmufs_mutex_unlock();
    return rv;
}

static int get_card_space(maple_device_t *dev, size_t *free_blocks,
                          size_t *executable_free_blocks) {
    vmu_root_t root;
    vmu_dir_t *dir = NULL;
    uint16_t *fat = NULL;
    vmufs_validation_t result;
    size_t dir_bytes;
    int rv = -1;

    vmufs_mutex_lock();
    if(vmufs_root_read(dev, &root) < 0 ||
       vmufs_root_validate(&root, VMUFS_STANDARD_CARD_BLOCKS) < 0)
        goto out;

    dir_bytes = (size_t)vmufs_dir_blocks(&root);
    dir = malloc(dir_bytes);
    fat = malloc(VMUFS_BLOCK_SIZE);
    if(!dir || !fat || vmufs_dir_read(dev, &root, dir) < 0 ||
       vmufs_fat_read(dev, &root, fat) < 0 ||
       vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat), dir,
                      dir_bytes / sizeof(*dir), &result) < 0)
        goto out;

    *free_blocks = result.free_blocks;
    *executable_free_blocks = result.executable_free_blocks;
    rv = 0;

out:
    free(fat);
    free(dir);
    vmufs_mutex_unlock();
    return rv;
}

static int verify_file(maple_device_t *dev, const char *name,
                       const uint8_t *expected, size_t expected_size) {
    uint8_t *readback = NULL;
    int read_size = 0;
    int rv = -1;

    if(vmufs_read(dev, name, (void **)&readback, &read_size) == 0 &&
       read_size == (int)expected_size &&
       memcmp(readback, expected, expected_size) == 0)
        rv = 0;

    free(readback);
    return rv;
}

static int test_maintenance(maple_device_t *dev) {
    uint8_t a[5 * VMUFS_BLOCK_SIZE];
    uint8_t b[5 * VMUFS_BLOCK_SIZE];
    uint8_t c[5 * VMUFS_BLOCK_SIZE];
    vmu_dir_t timestamp_source = {0};
    vmufs_format_options_t options = {0};
    vmufs_request_t *request;
    size_t free_blocks;
    size_t executable_free;
    int rv = -1;

    memset(a, 0x31, sizeof(a));
    memset(b, 0x62, sizeof(b));
    memset(c, 0x93, sizeof(c));
    vmufs_dir_fill_time(&timestamp_source);
    options.timestamp = timestamp_source.timestamp;

    request = vmufs_format_async(dev, &options, VMUFS_FORMAT_FULL,
                                 NULL, NULL);
    if(finish_maintenance_request(request, 257, 241) < 0) {
        rv = -21;
        goto out;
    }

    if(vmufs_write(dev, DEFRAG_A_NAME, a, sizeof(a), 0) < 0 ||
       vmufs_write(dev, DEFRAG_B_NAME, b, sizeof(b), 0) < 0 ||
       vmufs_write(dev, DEFRAG_C_NAME, c, sizeof(c), 0) < 0 ||
       vmufs_delete(dev, DEFRAG_B_NAME) < 0 ||
       get_card_space(dev, &free_blocks, &executable_free) < 0 ||
       free_blocks != 190 || executable_free != 185) {
        rv = -22;
        goto out;
    }

    request = vmufs_defragment_async(dev, NULL, NULL);
    if(finish_maintenance_request(request, 7, 5) < 0 ||
       get_card_space(dev, &free_blocks, &executable_free) < 0 ||
       free_blocks != 190 || executable_free != 190 ||
       verify_file(dev, DEFRAG_A_NAME, a, sizeof(a)) < 0 ||
       verify_file(dev, DEFRAG_C_NAME, c, sizeof(c)) < 0) {
        rv = -23;
        goto out;
    }

    request = vmufs_format_async(dev, &options, VMUFS_FORMAT_QUICK,
                                 NULL, NULL);
    if(finish_maintenance_request(request, 16, 0) < 0 ||
       get_card_space(dev, &free_blocks, &executable_free) < 0 ||
       free_blocks != VMUFS_STANDARD_USER_BLOCKS ||
       executable_free != VMUFS_STANDARD_USER_BLOCKS) {
        rv = -24;
        goto out;
    }

    rv = 0;

out:
    if(rv < 0) {
        vmufs_delete(dev, DEFRAG_A_NAME);
        vmufs_delete(dev, DEFRAG_B_NAME);
        vmufs_delete(dev, DEFRAG_C_NAME);
    }
    return rv;
}

static int test_low_level(maple_device_t *dev) {
    uint8_t first[700], second[900], *readback = NULL;
    callback_state_t callback = {.monotonic = true};
    vmufs_request_t *request;
    int read_size = 0;

    memset(first, 0x35, sizeof(first));
    memset(second, 0xa7, sizeof(second));
    vmufs_delete(dev, LOW_LEVEL_NAME);

    request = vmufs_write_async(dev, LOW_LEVEL_NAME, first, sizeof(first), 0,
                                request_callback, &callback);
    if(!request || finish_successful_request(request, &callback, 4, 2,
                                             true) < 0)
        return -1;

    callback = (callback_state_t){.monotonic = true};
    request = vmufs_write_async(dev, LOW_LEVEL_NAME, second, sizeof(second),
                                VMUFS_OVERWRITE,
                                request_callback, &callback);
    if(!request || finish_successful_request(request, &callback, 5, 2,
                                             true) < 0)
        return -1;

    if(vmufs_read(dev, LOW_LEVEL_NAME, (void **)&readback, &read_size) < 0 ||
       read_size != 1024 || memcmp(readback, second, sizeof(second)) != 0)
        goto fail;

    for(size_t i = sizeof(second); i < (size_t)read_size; ++i) {
        if(readback[i] != 0)
            goto fail;
    }

    free(readback);
    callback = (callback_state_t){.monotonic = true};
    request = vmufs_delete_async(dev, LOW_LEVEL_NAME,
                                 request_callback, &callback);
    return request
        ? finish_successful_request(request, &callback, 2, 0, false) : -1;

fail:
    free(readback);
    vmufs_delete(dev, LOW_LEVEL_NAME);
    return -1;
}

static int test_cancellation(maple_device_t *dev) {
    uint8_t data[4096];
    callback_state_t callback = {.monotonic = true};
    vmufs_request_status_t status;
    vmufs_request_t *request;
    void *readback = NULL;
    int read_size;

    memset(data, 0x5c, sizeof(data));
    vmufs_delete(dev, CANCEL_NAME);
    request = vmufs_write_async(dev, CANCEL_NAME, data, sizeof(data), 0,
                                request_callback, &callback);
    if(!request || vmufs_request_cancel(request) < 0 ||
       vmufs_request_wait(request, 15000, &status) < 0 ||
       status.state != VMUFS_REQUEST_CANCELLED || status.committed ||
       status.error != ECANCELED ||
       vmufs_request_wait_callback(request, 1000) < 0 ||
       !callback.monotonic || !callback.terminal ||
       vmufs_request_destroy(request) < 0)
        return -1;

    if(vmufs_read(dev, CANCEL_NAME, &readback, &read_size) >= 0) {
        free(readback);
        vmufs_delete(dev, CANCEL_NAME);
        return -1;
    }

    return 0;
}

static int test_vfs(maple_device_t *dev) {
    static const uint8_t payload[] = {'K', 'O', 'S', 'V', 'M', 'U', '!'};
    uint8_t readback[sizeof(payload) + 1u];
    char path[32];
    vmu_pkg_t pkg = {0};
    file_t fd;
    int rv = -1;

    snprintf(path, sizeof(path), "/vmu/%c%d/%s",
             dev->port + 'a', dev->unit, VFS_NAME);
    fs_unlink(path);

    memcpy(pkg.desc_short, "KOS VMU TEST", 12);
    memcpy(pkg.desc_long, "KOS filesystem validation", 25);
    memcpy(pkg.app_id, "KOS", 3);
    pkg.eyecatch_type = VMUPKG_EC_NONE;

    fd = fs_open(path, O_WRONLY | O_TRUNC);
    if(fd == FILEHND_INVALID)
        return -1;

    if(fs_vmu_set_header(fd, &pkg) < 0 ||
       fs_write(fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload)) {
        fs_close(fd);
        goto cleanup;
    }

    if(fs_close(fd) < 0)
        goto cleanup;

    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID)
        goto cleanup;

    memset(readback, 0, sizeof(readback));
    if(fs_total(fd) != (ssize_t)sizeof(payload) ||
       fs_read(fd, readback, sizeof(readback)) != (ssize_t)sizeof(payload) ||
       memcmp(readback, payload, sizeof(payload)) != 0) {
        fs_close(fd);
        goto cleanup;
    }

    if(fs_close(fd) < 0)
        goto cleanup;

    /* A configured default header must not take ownership of a raw handle's
       cache. This close used to free the same buffer twice. */
    if(fs_vmu_set_default_header(&pkg) < 0)
        goto cleanup;

    fd = fs_open(path, O_WRONLY | O_TRUNC | O_META);
    if(fd == FILEHND_INVALID)
        goto clear_default;

    if(fs_write(fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload)) {
        fs_close(fd);
        goto clear_default;
    }

    if(fs_close(fd) < 0)
        goto clear_default;

    rv = 0;

clear_default:
    fs_vmu_set_default_header(NULL);
cleanup:
    fs_unlink(path);
    return rv;
}

static int write_pass_marker(maple_device_t *dev) {
    static const uint8_t marker[] = {'K', 'O', 'S', 'V', 'M', 'U', '!'};

    vmufs_delete(dev, PASS_NAME);
    return vmufs_write(dev, PASS_NAME, (void *)marker, sizeof(marker), 0);
}

static int report_failure(maple_device_t *dev, uint8_t stage) {
    uint8_t marker[8] = {'K', 'O', 'S', 'F', 'A', 'I', 'L', stage};

    vmufs_delete(dev, FAIL_NAME);
    vmufs_write(dev, FAIL_NAME, marker, sizeof(marker), 0);
    printf("RESULT: FAIL (stage %u)\n", stage);
    return 1;
}

int main(void) {
    maple_device_t *dev;
    int maintenance_result;

    dbgio_dev_select("scif");
    dev = first_card();

    if(!dev) {
        puts("RESULT: FAIL (no memory card)");
        return 1;
    }

    printf("testing %c%d\n", dev->port + 'A', dev->unit);
    if(validate_card(dev) < 0)
        return report_failure(dev, 1);
    maintenance_result = test_maintenance(dev);
    if(maintenance_result < 0)
        return report_failure(dev, (uint8_t)-maintenance_result);
    if(test_cancellation(dev) < 0)
        return report_failure(dev, 3);
    if(test_low_level(dev) < 0)
        return report_failure(dev, 4);
    if(test_vfs(dev) < 0)
        return report_failure(dev, 5);
    if(validate_card(dev) < 0)
        return report_failure(dev, 6);
    if(write_pass_marker(dev) < 0)
        return report_failure(dev, 7);
    if(validate_card(dev) < 0)
        return report_failure(dev, 8);

    puts("RESULT: PASS");
    return 0;
}
