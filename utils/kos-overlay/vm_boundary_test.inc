/* Original host-only VM boundary model for the actual overlay consumer. */
struct vm_exec_region {
    void *base;
    size_t bytes;
    uint8_t *data;
    bool published;
};
struct vm_exec_load { vm_exec_region_t *region; bool begun; };
static int vm_fail, vm_abort_fail, vm_retire_fail, vm_context_bad, vm_publishes;
static ovl_slot_t *vm_observed;
static void (*vm_write_observer)(void);

int vm_exec_reserve(vm_space_t *space, void *base, size_t bytes, vm_exec_region_t **out) {
    assert(!lock_depth && space && bytes == 8192 && base == (void *)0x101ff000);
    vm_exec_region_t *r = calloc(1, sizeof(*r));
    assert(r);
    r->base = base; r->bytes = bytes; *out = r;
    return 0;
}
int vm_exec_check_context(vm_exec_region_t *r) {
    assert(r);
    return vm_context_bad ? error(EBUSY) : 0;
}
int vm_exec_prepare(vm_exec_region_t *r, const uint8_t *plan, size_t count,
                      vm_exec_load_t **out) {
    assert(!lock_depth && r && count == 2 && plan[0] == 5 && plan[1] == 6);
    if(vm_exec_check_context(r) < 0) return -1;
    if(vm_fail == 1) return error(ENOMEM);
    vm_exec_load_t *l = calloc(1, sizeof(*l));
    assert(l); l->region = r; *out = l;
    return 0;
}
int vm_exec_begin(vm_exec_load_t *l) {
    assert(!lock_depth && l && !l->begun);
    if(vm_fail == 6) return error(EBUSY); /* Refusal leaves PREPARED intact. */
    l->begun = true;
    if(vm_fail == 2) return error(EIO); /* Irreversible even with old bytes intact. */
    free(l->region->data);
    l->region->data = calloc(1, l->region->bytes);
    assert(l->region->data);
    l->region->published = false;
    return 0;
}
int vm_exec_write(vm_exec_load_t *l, size_t offset, const void *data, size_t bytes) {
    assert(!lock_depth && l && l->begun && offset <= l->region->bytes);
    assert(bytes <= l->region->bytes - offset && !l->region->published);
    if(vm_write_observer) vm_write_observer();
    if(vm_fail == 3) {
        if(bytes) l->region->data[offset] = *(const uint8_t *)data;
        return error(EIO);
    }
    memcpy(l->region->data + offset, data, bytes);
    return 0;
}
int vm_exec_zero(vm_exec_load_t *l, size_t offset, size_t bytes) {
    assert(!lock_depth && l && l->begun && offset <= l->region->bytes);
    assert(bytes <= l->region->bytes - offset);
    if(vm_fail == 4) return error(EIO);
    memset(l->region->data + offset, 0, bytes);
    return 0;
}
int vm_exec_publish(vm_exec_load_t **l) {
    assert(!lock_depth && !live_fd && l && *l && (*l)->begun);
    if(vm_fail == 5) return error(EIO);
    (*l)->region->published = true;
    free(*l); *l = NULL; ++vm_publishes;
    return 0;
}
int vm_exec_abort(vm_exec_load_t **l) {
    assert(!lock_depth && !live_fd && l && *l);
    if(vm_abort_fail) return error(EAGAIN);
    if((*l)->begun) {
        free((*l)->region->data); (*l)->region->data = NULL;
        (*l)->region->published = false;
    }
    free(*l); *l = NULL;
    return 0;
}
int vm_exec_retire(vm_exec_region_t *r) {
    assert(!lock_depth && !live_fd && r);
    if(vm_retire_fail) return error(EIO);
    free(r->data); r->data = NULL; r->published = false;
    return 0;
}
int vm_exec_destroy(vm_exec_region_t **r) {
    assert(!lock_depth && r && *r && !(*r)->published && !(*r)->data);
    free(*r); *r = NULL;
    return 0;
}

static void vm_observer(ovl_slot_t *slot, bool loaded, void *context) {
    assert(!lock_depth && !cache_calls && context == &trusted);
    if(loaded) { assert(!live_fd && slot->region->published); ++loads; }
    else ++unloads;
    busy_checks(slot);
}
static void vm_busy_writer(void) { busy_checks(vm_observed); }

static void vm_runtime_tests(void) {
    ovl_slot_options_t options = {(void *)0x101ff000, 8192, vm_observer, &trusted};
    ovl_slot_t *slot = NULL;
    ovl_load_result_t result;
    int space_identity;
    reset_io();
    assert(!ovl_slot_create_vm((vm_space_t *)&space_identity, &options, &slot));
    assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));
    assert(result.image_admitted && loads == 1 && closes == 1 && vm_publishes == 1);
    assert(!cache_calls && !slot->load && slot->active);
    assert(!memcmp(slot->region->data, image + 128 + 0x100, 4));
    assert(!memcmp(slot->region->data + 4096, image + 128 + 0x104, 4));
    for(size_t i = 4100; i < 8192; ++i) assert(!slot->region->data[i]);

    ovl_token_t token = {0};
    ovl_view_t view, before;
    memset(&view, 0xa7, sizeof(view)); before = view;
    vm_context_bad = 1;
    assert(ovl_acquire(slot, &token, &view) < 0 && errno == EBUSY);
    assert(!memcmp(&view, &before, sizeof(view)) && !token._slot);
    reset_io();
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EBUSY);
    assert(slot->active && slot->generation == 1 && !unloads && closes == 1);
    vm_context_bad = 0;
    assert(!ovl_acquire(slot, &token, &view));
    assert(ovl_unload(slot) < 0 && errno == EBUSY);
    assert(!ovl_release(slot, &token));
    vm_fail = 1; reset_io();
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == ENOMEM);
    assert(slot->active && slot->generation == 1 && !slot->load && !unloads);
    vm_fail = 0;

    reset_io();
    image[28] = OVL_PROFILE_P1;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == ENOEXEC);
    assert(slot->active && slot->generation == 1 && !unloads && !slot->load);
    image[28] = OVL_PROFILE_VM;
    for(unsigned fault = 0; fault < 3; ++fault) {
        reset_io();
        if(fault == 0) fail_at = 129;
        if(fault == 1) image[image_bytes - 1] ^= 1;
        if(fault == 2) close_error = ENOSPC;
        int published_before = vm_publishes;
        assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0);
        assert(!result.image_admitted && !slot->active && !slot->cleanup_pending);
        assert(!slot->load && !slot->region->data && closes == 1 && unloads == 1);
        assert(!loads && vm_publishes == published_before);
        if(fault == 1) image[image_bytes - 1] ^= 1;
        reset_io();
        assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));
    }
    for(int fault = 2; fault <= 5; ++fault) {
        reset_io(); vm_fail = fault; vm_abort_fail = 1;
        if(fault == 3) close_error = ENOSPC;
        assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EIO);
        assert(result.error == EIO && result.source_close_error == (fault == 3 ? ENOSPC : 0));
        assert(!result.image_admitted && !loads && unloads == 1 && closes == 1);
        assert(!slot->active && slot->cleanup_pending && slot->load && slot->region->data);
        assert(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
        int old_opens = opens;
        assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EBUSY);
        assert(opens == old_opens);
        assert(ovl_unload(slot) < 0 && errno == EAGAIN && unloads == 1);
        vm_abort_fail = 0; vm_retire_fail = 1;
        assert(ovl_unload(slot) < 0 && errno == EIO);
        assert(!slot->load && slot->cleanup_pending && unloads == 1);
        vm_retire_fail = 0; vm_fail = 0;
        assert(!ovl_unload(slot) && !slot->cleanup_pending && !slot->region->data);
        reset_io();
        assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));
        assert(result.image_admitted && !unloads && loads == 1);
    }
    /* Notification has closed admission, but PREPARED abort preserves the old
       published backing. Retirement is still required, including on retry. */
    reset_io(); vm_fail = 6; vm_retire_fail = 1;
    uint8_t *old_data = slot->region->data;
    uint64_t old_generation = slot->generation;
    int old_publishes = vm_publishes;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EBUSY);
    assert(result.error == EBUSY && !result.source_close_error && !result.image_admitted);
    assert(!slot->active && slot->cleanup_pending && !slot->load);
    assert(slot->region->published && slot->region->data == old_data);
    assert(slot->generation == old_generation && vm_publishes == old_publishes);
    assert(!loads && unloads == 1 && closes == 1);
    assert(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    int old_opens = opens;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EBUSY);
    assert(opens == old_opens);
    assert(ovl_unload(slot) < 0 && errno == EIO && unloads == 1);
    assert(slot->region->published && slot->region->data == old_data);
    vm_fail = 0; vm_retire_fail = 0;
    assert(!ovl_unload(slot) && unloads == 1 && closes == 1);
    assert(!slot->cleanup_pending && !slot->region->published && !slot->region->data);
    reset_io();
    assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));

    reset_io(); vm_observed = slot; vm_write_observer = vm_busy_writer;
    assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));
    vm_write_observer = NULL;
    reset_io(); vm_retire_fail = 1;
    assert(ovl_unload(slot) < 0 && errno == EIO && unloads == 1);
    assert(!slot->active && slot->cleanup_pending && slot->region->data);
    assert(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    vm_retire_fail = 0;
    assert(!ovl_unload(slot) && unloads == 1);
    assert(!ovl_slot_destroy(slot));
    puts("PASS: actual VM consumer against fault-injected boundary model; no translated execution");
}
