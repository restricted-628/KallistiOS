/* KallistiOS ##version##

   nmmgr-lifecycle-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/nmmgr.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

typedef struct remove_context {
    nmmgr_handler_t *handler;
    int result;
    int error;
} remove_context_t;

static nmmgr_handler_t make_handler(const char *path, uint32_t flags,
                                    uint32_t type) {
    nmmgr_handler_t handler = {
        .pid = 0,
        .version = UINT32_C(0x00010000),
        .flags = flags,
        .type = type,
        .list_ent = NMMGR_LIST_INIT,
    };

    strcpy(handler.pathname, path);
    return handler;
}

static void sleep_milliseconds(long milliseconds) {
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = milliseconds % 1000 * 1000000L,
    };

    nanosleep(&delay, NULL);
}

static void *remove_thread(void *argument) {
    remove_context_t *context = argument;

    context->result = nmmgr_handler_remove(context->handler);
    context->error = errno;
    return NULL;
}

static void test_add_lookup_and_snapshot(void) {
    nmmgr_handler_t root = make_handler("/test", 0, NMMGR_TYPE_VFS);
    nmmgr_handler_t device = make_handler("/dev/test", NMMGR_FLAGS_INDEV,
                                          NMMGR_TYPE_SINGLETON);
    nmmgr_handler_t duplicate = make_handler("/TEST", 0,
                                             NMMGR_TYPE_VFS);
    char path[NAME_MAX];
    nmmgr_handler_t *reference;

    CHECK(nmmgr_handler_add(&root) == 0);
    CHECK(nmmgr_handler_add(&device) == 0);
    errno = 0;
    CHECK(nmmgr_handler_add(&duplicate) < 0 && errno == EEXIST);

    reference = nmmgr_lookup_ref("/test/file");
    CHECK(reference == &root);
    CHECK(nmmgr_handler_release(reference) == 0);

    CHECK(nmmgr_handler_get_path(0, NMMGR_TYPE_SINGLETON,
                                 NMMGR_FLAGS_INDEV, 0, path,
                                 sizeof(path)) == 0);
    CHECK(!strcmp(path, "/dev/test"));
    CHECK(nmmgr_handler_remove(&device) == 0);
    CHECK(nmmgr_handler_remove(&root) == 0);
}

static void test_timeout_unpublishes(void) {
    nmmgr_handler_t handler = make_handler("/timeout", 0, NMMGR_TYPE_VFS);
    nmmgr_handler_t *reference;

    CHECK(nmmgr_handler_add(&handler) == 0);
    reference = nmmgr_lookup_ref("/timeout/file");
    CHECK(reference == &handler);

    errno = 0;
    CHECK(nmmgr_handler_remove_timed(&handler, 20) < 0);
    CHECK(errno == ETIMEDOUT);
    errno = 0;
    CHECK(nmmgr_lookup_ref("/timeout/file") == NULL && errno == ENOENT);

    CHECK(nmmgr_handler_release(reference) == 0);
    CHECK(nmmgr_handler_remove(&handler) == 0);
}

static void test_blocking_drain(void) {
    nmmgr_handler_t handler = make_handler("/drain", 0, NMMGR_TYPE_VFS);
    remove_context_t context = {
        .handler = &handler,
        .result = -2,
    };
    nmmgr_handler_t *reference;
    pthread_t thread;

    CHECK(nmmgr_handler_add(&handler) == 0);
    reference = nmmgr_lookup_ref("/drain/file");
    CHECK(reference == &handler);
    CHECK(pthread_create(&thread, NULL, remove_thread, &context) == 0);
    sleep_milliseconds(20);

    errno = 0;
    CHECK(nmmgr_lookup_ref("/drain/file") == NULL && errno == ENOENT);
    CHECK(context.result == -2);
    CHECK(nmmgr_handler_release(reference) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(context.result == 0);
}

static void test_alias_retains_target(void) {
    nmmgr_handler_t target = make_handler("/target", 0, NMMGR_TYPE_VFS);
    alias_handler_t alias = {
        .nmmgr = {
            .pathname = "/alias",
            .pid = 0,
            .version = UINT32_C(0x00010000),
            .flags = NMMGR_FLAGS_ALIAS,
            .type = NMMGR_TYPE_VFS,
            .list_ent = NMMGR_LIST_INIT,
        },
        .alias = &target,
    };
    nmmgr_handler_t *reference;

    CHECK(nmmgr_handler_add(&target) == 0);
    CHECK(nmmgr_handler_add(&alias.nmmgr) == 0);
    reference = nmmgr_lookup_ref("/alias/file");
    CHECK(reference == &target);
    CHECK(nmmgr_handler_remove(&alias.nmmgr) == 0);
    CHECK(nmmgr_handler_release(reference) == 0);
    CHECK(nmmgr_handler_remove(&target) == 0);
}

int main(void) {
    nmmgr_init();
    test_add_lookup_and_snapshot();
    test_timeout_unpublishes();
    test_blocking_drain();
    test_alias_retains_target();
    nmmgr_shutdown();

    if(failures) {
        fprintf(stderr, "%d name-manager lifecycle checks failed\n", failures);
        return 1;
    }

    puts("name-manager lifecycle tests passed");
    return 0;
}
