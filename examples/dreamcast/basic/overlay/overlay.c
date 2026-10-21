/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/fiber.h>
#include <kos/linker.h>
#include <kos/overlay.h>
#include <dc/fs_iso9660.h>
#include <arch/arch.h>
#include <arch/stack.h>
#include <errno.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>
#include "member-api.h"
#include "dco_project.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* The project-generated linker reservation owns this fixed P1 slot. */
#define SLOT_BASE (dco_project_slots[DCO_SLOT_GAME].base)
#define SLOT_BYTES (dco_project_slots[DCO_SLOT_GAME].bytes)
static alignas(32) unsigned char child_stack[16384];
static ovl_slot_t *slot;
static ovl_token_t running_token;
static ovl_token_t escaped_token;
static ovl_view_t running_view;
static kfiber_t *main_fiber;
static unsigned failures, loaded, unloaded, yields, blocked, calls;
static bool finished;
static int returned;

#define CHECK(test) do { if(!(test)) { \
    printf("OVL2 CHECK line=%d errno=%d\n", __LINE__, errno); \
    ++failures; } } while(0)

static void observer(ovl_slot_t *which, bool active, void *context) {
    (void)context;
    CHECK(which == slot);
    if(active) ++loaded;
    else ++unloaded;
    /* Notifications must not reopen admission early. */
    ovl_token_t token = {0};
    ovl_view_t view;
    CHECK(ovl_acquire(slot, &token, &view) == -1 && errno == EBUSY);
}

static int yield_to_resident(void) {
    ++yields;
    thd_pass();
    return fiber_switch(main_fiber);
}

static void resident_wrapper(void *unused) {
    (void)unused;
    member_context_t context = {yield_to_resident, MEMBER_COOKIE, 1};
    returned = running_view.entry(&context);
    ++calls;
    /* No return address into the overlay remains on this path. */
    CHECK(ovl_release(slot, &running_token) == 0);
    finished = true;
}

static int load_member(const char *path, unsigned index) {
    ovl_load_result_t result;
    printf("OVL2 SOURCE %s\n", path);
    int rc = ovl_load(slot, path, &dco_project_members[index].expected, &result);
    if(rc || !result.image_admitted || result.source_close_error) {
        printf("OVL2 LOAD_ERROR primary=%d close=%d\n", result.error,
               result.source_close_error);
        ++failures;
        return -1;
    }
    return 0;
}

static void run_suspended(int expected, uint64_t generation) {
    ovl_view_t view;
    if(ovl_acquire(slot, &running_token, &running_view) < 0) { CHECK(false); return; }
    CHECK(running_view.generation == generation && running_view.base == SLOT_BASE);
    /* An independently escaped data reference survives the executing fiber. */
    if(ovl_acquire(slot, &escaped_token, &view) < 0) { CHECK(false); return; }
    finished = false;
    kfiber_t *child = fiber_create(child_stack, sizeof(child_stack), resident_wrapper, NULL);
    if(!child) { CHECK(child != NULL); return; }
    if(fiber_switch(child) < 0 || finished) { CHECK(false); return; }
    ovl_load_result_t result;
    if(ovl_load(slot, "/cd/B.DCO", &dco_project_members[DCO_MEMBER_B].expected, &result) == -1 && errno == EBUSY)
        ++blocked;
    else CHECK(false);
    if(ovl_unload(slot) == -1 && errno == EBUSY) ++blocked;
    else CHECK(false);
    if(fiber_switch(child) < 0 || !finished ||
       fiber_get_state(child) != KFIBER_STATE_FINISHED) { CHECK(false); return; }
    CHECK(returned == expected);
    CHECK(fiber_destroy(child) == 0);
    if(ovl_unload(slot) == -1 && errno == EBUSY) ++blocked;
    else CHECK(false);
    CHECK(ovl_release(slot, &escaped_token) == 0);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("OVL2 START slot=%p bytes=%u heap=%p\n", SLOT_BASE,
           (unsigned)SLOT_BYTES, end);
    CHECK((uintptr_t)SLOT_BASE + SLOT_BYTES <= (uintptr_t)end);
    CHECK((uintptr_t)end < _arch_mem_top - THD_KERNEL_STACK_SIZE);
    CHECK(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) == 0);
    int direct = fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT;
    CHECK(direct);
    printf("OVL2 BACKEND direct=%d\n", direct);
    ovl_slot_options_t options = dco_project_slots[DCO_SLOT_GAME];
    options.observer = observer;
    if(ovl_slot_create(&options, &slot) < 0) { CHECK(false); goto done; }
    main_fiber = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!main_fiber) { CHECK(false); goto done; }
    if(load_member("/cd/A.DCO", DCO_MEMBER_A) < 0) goto done;
    run_suspended(101, 1);
    if(failures) goto done;

    ovl_load_result_t result;
    CHECK(ovl_load(slot, "/cd/BADHEAD.DCO", &dco_project_members[DCO_MEMBER_A].expected, &result) == -1);
    CHECK(!result.image_admitted && result.error == ENOEXEC && !result.source_close_error);
    ovl_token_t token = {0};
    ovl_view_t view;
    if(ovl_acquire(slot, &token, &view) < 0) { CHECK(false); goto done; }
    CHECK(view.generation == 1);
    member_context_t context = {yield_to_resident, MEMBER_COOKIE, 0};
    CHECK(view.entry(&context) == 102); /* Preparation failure retained A's BSS. */
    ++calls;
    CHECK(ovl_release(slot, &token) == 0);
    if(load_member("/cd/B.DCO", DCO_MEMBER_B) < 0) goto done;
    run_suspended(201, 2);
    if(failures) goto done;
    CHECK(ovl_load(slot, "/cd/BADHASH.DCO", &dco_project_members[DCO_MEMBER_A].expected, &result) == -1);
    CHECK(!result.image_admitted && result.error == ENOEXEC && !result.source_close_error);
    CHECK(ovl_acquire(slot, &token, &view) == -1 && errno == ENOENT);
    if(load_member("/cd/A.DCO", DCO_MEMBER_A) < 0) goto done;
    run_suspended(101, 3); /* Exact BSS reinitialized, new generation. */
    if(failures) goto done;
    CHECK(ovl_unload(slot) == 0);
    CHECK(ovl_slot_destroy(slot) == 0);
done:
    printf("OVL2 RESULT failures=%u loads=%u unloads=%u yields=%u blocked=%u calls=%u direct=%d\n",
           failures, loaded, unloaded, yields, blocked, calls, direct);
    fflush(stdout);
    return failures ? 1 : 0;
}
