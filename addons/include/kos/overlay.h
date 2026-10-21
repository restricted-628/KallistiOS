/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef __KOS_OVERLAY_H
#define __KOS_OVERLAY_H

#include <kos/cdefs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_DECLS

/** Optional, fixed-linked P1 overlays; link -lkosoverlay. No executable VM,
 * relocation, implicit fiber adapter or new physical-drive scheduler.
 * All calls require a normal thread with interrupts enabled, outside blocking-
 * prohibited contexts. Objects, stacks, callbacks and arguments must be resident
 * and physically disjoint from the slot (including any translated aliases).
 * No forced carrier/fiber destruction, nonlocal exits or foreign genwait error
 * wakes. Callers externally synchronize shared token storage and final teardown.
 */
typedef struct ovl_slot ovl_slot_t;
typedef int (*ovl_entry_t)(void *context);
typedef void (*ovl_observer_t)(ovl_slot_t *slot, bool loaded, void *context);

typedef struct ovl_slot_options {
    void *base;
    size_t bytes;
    ovl_observer_t observer;
    void *context;
} ovl_slot_options_t;

typedef struct ovl_expected {
    uint32_t package_bytes;
    uint8_t sha256[32];
} ovl_expected_t;

typedef struct ovl_load_result {
    bool image_admitted;
    int error;
    int source_close_error;
} ovl_load_result_t;

/** Zero-initialize. Private fields: do not edit, copy or move a live token.
 * Reuse only after every borrower/API caller of its previous use has retired.
 * There is no stale-pointer protection after forbidden storage reuse/free.
 */
typedef struct ovl_token {
    ovl_slot_t *_slot;
    struct ovl_token *_next;
    uint64_t _generation;
} ovl_token_t;

typedef struct ovl_view {
    void *base;
    size_t bytes;
    ovl_entry_t entry;
    uint64_t generation;
    ovl_expected_t identity;
} ovl_view_t;

/** Caller reserves a 32-byte-aligned, nonzero, <=1 MiB ordinary P1 RAM span.
 * Prove by linker map that it precedes heap end, excludes resident code/data/
 * stacks and lies below the kernel-stack reserve. Address validation is NOT
 * reservation. No other slot may overlap it. Failure leaves *out unchanged.
 * Observer is an optional normally returning resident notification, never an
 * execution or breakpoint hook: no slot writes/execution or waiting for the
 * same operation. Same-slot API reentry returns EBUSY during notifications.
 */
int ovl_slot_create(const ovl_slot_options_t *options, ovl_slot_t **out);

/** Synchronous private read of immutable regular ISO9660 /cd content on the
 * direct backend. Keep mount/media/backend and all input objects stable through
 * return. No global VFS teardown or external descriptor operations. Expected
 * length/digest comes from trusted resident metadata, not the package itself.
 * Before old-image retirement, failure preserves it; after retirement, any
 * failure leaves EMPTY. Close runs once; its error is reported independently.
 * Blocks the carrier; no cancellation or total deadline. Result is required.
 */
int ovl_load(ovl_slot_t *slot, const char *path, const ovl_expected_t *expected,
             ovl_load_result_t *result);

/** Acquire before publishing entry/data pointers, creating a fiber or arming a
 * callback. Hold across resident helper calls, suspension and preemption. Each
 * escaped pointer/callback owner needs residency through its final return.
 * Release from a resident outer wrapper, never from an overlay returning into
 * itself. Failed acquire preserves token and view. No overlay IRQ handlers.
 */
int ovl_acquire(ovl_slot_t *slot, ovl_token_t *token, ovl_view_t *view);
int ovl_release(ovl_slot_t *slot, ovl_token_t *token);

/** EBUSY while any token/operation remains. EMPTY is a successful no-op.
 * Does not erase bytes or reset the monotonically increasing generation. */
int ovl_unload(ovl_slot_t *slot);

/** EMPTY/no tokens/no operation only. Externally join ALL API callers/waiters
 * before destroy. Frees metadata, not caller RAM. This is not a join primitive. */
int ovl_slot_destroy(ovl_slot_t *slot);

__END_DECLS
#endif
