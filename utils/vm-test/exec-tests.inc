/* Actual executable-region/MMU code; only platform effects/failures are modeled. */
static vm_space_t *exec_space(uintptr_t base, size_t bytes) {
    vm_space_t *s = vm_space_create(context(0), P(base), bytes);
    CHECK(s);
    map_func = map_virt; mmucr_value = 1;
    mmu_use_table(contexts[0]); mmu_switch_context(contexts[0]);
    exec_maps = exec_unmaps = fail_exec_map = fail_exec_unmap = 0;
    return s;
}
static vm_exec_info_t exec_info(vm_exec_region_t *e, vm_exec_phase_t phase, int pub) {
    vm_exec_info_t i;
    CHECK(!vm_exec_query(e, &i) && i.phase == phase && i.published == pub);
    return i;
}
static void exec_end(vm_space_t *s, vm_exec_region_t **e) {
    CHECK(!vm_exec_retire(*e));
    CHECK(!vm_exec_destroy(e) && !*e);
    CHECK(!vm_space_destroy(s)); drop(0); CHECK(!live);
    reset_failure();
}
static vm_exec_load_t *exec_writing(vm_exec_region_t *e, const uint8_t *plan, size_t n) {
    vm_exec_load_t *l = NULL;
    CHECK(!vm_exec_prepare(e, plan, n, &l) && l);
    CHECK(!vm_exec_begin(l));
    exec_info(e, VM_EXEC_WRITING, 0);
    return l;
}
static void exec_layout_and_data(void) {
    vm_space_t *s = exec_space(BASE, 8192);
    vm_exec_region_t *e = NULL, *sentinel = (void *)0x1234;
    CHECK(vm_exec_reserve(s, NULL, 4096, &sentinel) == -1 && errno == EINVAL);
    CHECK(sentinel == (void *)0x1234);
    CHECK(vm_exec_reserve(s, P(BASE + 1), 4096, &sentinel) == -1 && errno == EINVAL);
    CHECK(vm_exec_reserve(s, P(BASE), 1048577, &sentinel) == -1 && errno == EINVAL);
    CHECK(!vm_exec_reserve(s, P(BASE), 8192, &e));
    vm_exec_info_t initial = exec_info(e, VM_EXEC_EMPTY, 0);
    CHECK(initial.table_budget_bytes == 2 * sizeof(mmusubcontext_t));
    CHECK(vm_exec_reserve(s, P(BASE), 4096, &sentinel) == -1 && errno == EEXIST);
    CHECK(vm_space_destroy(s) == -1 && errno == EBUSY);
    void *v;
    CHECK(vm_reserve(s, P(BASE), 4096, &v) == -1 && errno == EEXIST);
    CHECK(vm_commit(s, P(BASE), 4096, VM_READ) == -1 && errno == EINVAL);
    CHECK(vm_protect(s, P(BASE), 4096, VM_READWRITE) == -1 && errno == EINVAL);
    CHECK(vm_decommit(s, P(BASE), 4096) == -1 && errno == EINVAL);
    CHECK(vm_release(s, P(BASE)) == -1 && errno == EINVAL);
    vm_info_t q = {.region_size = 123};
    CHECK(vm_query(s, P(BASE), &q) == -1 && errno == EINVAL && q.region_size == 123);
    vm_fault_t *fault = NULL;
    CHECK(vm_fault_arm(s, P(BASE), VM_FAULT_ZERO_COMMIT, VM_READ, &fault) == -1 && errno == EINVAL);
    vm_pin_t *pin = NULL;
    CHECK(vm_pin_acquire(s, P(BASE), 7, VM_PIN_READ, &pin) == -1 && errno == EINVAL);
    vm_file_options_t opt = {.requested=P(BASE),.bytes=1,.resident_limit=1,.budget_bytes=65536};
    vm_file_t *file = NULL;
    CHECK(vm_file_open(s, "/test", &opt, &file) == -1 && errno == EINVAL && !file);
    const uint8_t bad[][2] = {{0,0},{4,6},{5,7}};
    vm_exec_load_t *l = (void *)0x4321;
    for(unsigned i = 0; i < 3; ++i) {
        CHECK(vm_exec_prepare(e, bad[i], 2, &l) == -1 && errno == EINVAL);
        CHECK(l == (void *)0x4321);
        CHECK(exec_info(e, VM_EXEC_EMPTY, 0).metadata_bytes == initial.metadata_bytes);
    }
    const uint8_t plan[] = {5,6};
    l = exec_writing(e, plan, 2);
    CHECK(vm_exec_begin(l) == -1 && errno == EBUSY);
    CHECK(vm_exec_retire(e) == -1 && errno == EBUSY);
    CHECK(vm_exec_destroy(&e) == -1 && errno == EBUSY && e);
    unsigned char src[4200];
    for(size_t i = 0; i < sizeof(src); ++i) src[i] = (i * 13 + 1) & 255;
    CHECK(!vm_exec_write(l, 3990, src, sizeof(src)));
    CHECK(!vm_exec_zero(l, 4090, 14));
    for(size_t i = 0; i < 8192; ++i) {
        unsigned expected = i >= 3990 && i < 8190 ? src[i-3990] : 0;
        if(i >= 4090 && i < 4104) expected = 0;
        CHECK(((unsigned char *)e->region->pages[i/4096].backing)[i%4096] == expected);
    }
    memset(src, 0xee, sizeof(src)); /* Writer has consumed it; no later read. */
    CHECK(!vm_exec_zero(l, 8192, 0));
    CHECK(!vm_exec_write(l, 8192, NULL, 0));
    CHECK(!vm_exec_publish(&l) && !l);
    CHECK(exec_info(e, VM_EXEC_READY, 1).metadata_bytes == initial.metadata_bytes);
    CHECK(map_virt(contexts[0], BASE/4096)->prkey == MMU_KERNEL_RDONLY);
    CHECK(map_virt(contexts[0], BASE/4096+1)->prkey == MMU_KERNEL_RDWR);
    CHECK(!vm_exec_prepare(e, plan, 2, &l));
    CHECK(exec_info(e, VM_EXEC_PREPARED, 1).backing_bytes == 8192);
    CHECK(!vm_exec_abort(&l) && !l); exec_info(e, VM_EXEC_READY, 1);
    /* Destructive A/B/A ownership with distinct bytes; no host execution claim. */
    for(unsigned i = 0; i < 3; ++i) {
        l = exec_writing(e, plan, 2);
        uint32_t value = (i == 1) ? 222 : 111;
        CHECK(!vm_exec_write(l, 0, &value, sizeof(value)));
        CHECK(!vm_exec_publish(&l));
        CHECK(*(uint32_t *)e->region->pages[0].backing == value);
    }
    exec_end(s, &e);
}
static void exec_context_and_boundary(void) {
    vm_space_t *s = exec_space(0x1ffff000u, 8192);
    vm_exec_region_t *e;
    CHECK(!vm_exec_reserve(s, P(0x1ffff000u), 8192, &e));
    const uint8_t plan[] = {5,6};
    vm_exec_load_t *l = NULL;
    uint32_t masks[] = {0x10000000u, 0x10u, 0xf0u};
    for(unsigned i = 0; i < 3; ++i) {
        test_sr = masks[i];
        CHECK(vm_exec_prepare(e, plan, 2, &l) == -1 && errno == EPERM && !l);
    }
    test_sr = 0; test_irq = 1;
    CHECK(vm_exec_retire(e) == -1 && errno == EPERM); test_irq = 0;
    mmucontext_t other = {.asid=contexts[0]->asid};
    mmu_cxt_current = &other;
    CHECK(vm_exec_check_context(e) == -1 && errno == EBUSY);
    mmu_cxt_current = contexts[0]; pteh_value ^= 1;
    CHECK(vm_exec_check_context(e) == -1 && errno == EBUSY);
    pteh_value ^= 1;
    mmucr_value = 0; CHECK(vm_exec_check_context(e) == -1 && errno == EBUSY);
    mmucr_value = 0x101; CHECK(vm_exec_check_context(e) == -1 && errno == EBUSY);
    mmucr_value = 1; map_func = NULL;
    CHECK(vm_exec_check_context(e) == -1 && errno == EBUSY); map_func = map_virt;
    CHECK(!vm_exec_check_context(e));
    l = exec_writing(e, plan, 2); CHECK(!vm_exec_publish(&l));
    CHECK(!vm_exec_prepare(e, plan, 2, &l));
    test_sr = 0x10;
    CHECK(vm_exec_begin(l) == -1 && errno == EPERM); test_sr = 0;
    exec_info(e, VM_EXEC_PREPARED, 1);
    /* A valid begin attempt is irreversible BEFORE a failed context check. */
    mmu_cxt_current = &other;
    CHECK(vm_exec_begin(l) == -1 && errno == EBUSY);
    exec_info(e, VM_EXEC_FAILED, 0);
    uint32_t saved_pteh = pteh_value;
    CHECK(!vm_exec_abort(&l) && !l && pteh_value == saved_pteh);
    CHECK(!e->info.backing_bytes && mmu_cxt_current == &other);
    mmu_cxt_current = contexts[0];
    l = exec_writing(e, plan, 2); mmucr_value = 0;
    CHECK(vm_exec_publish(&l) == -1 && errno == EBUSY && l);
    exec_info(e, VM_EXEC_FAILED, 0);
    CHECK(!vm_exec_abort(&l)); mmucr_value = 1;
    exec_end(s, &e);
}
static vm_exec_region_t *exec_watched;
static void exec_outside_lock(void) {
    CHECK(!pthread_mutex_trylock(&exec_watched->space->mutex));
    CHECK(!pthread_mutex_unlock(&exec_watched->space->mutex));
    vm_exec_info_t i;
    CHECK(!vm_exec_query(exec_watched, &i));
    CHECK(vm_exec_retire(exec_watched) == -1 && errno == EBUSY);
}
static void exec_failures(void) {
    vm_space_t *s = exec_space(BASE, 8192);
    vm_exec_region_t *e;
    CHECK(!vm_exec_reserve(s, P(BASE), 8192, &e));
    const uint8_t plan[] = {5,6};
    unsigned baseline = live;
    for(unsigned fail = 1; fail <= 3; ++fail) {
        reset_failure(); fail_at = fail;
        vm_exec_load_t *l = (void *)0x1234;
        CHECK(vm_exec_prepare(e, plan, 2, &l) == -1 && errno == ENOMEM);
        CHECK(l == (void *)0x1234 && live == baseline);
        exec_info(e, VM_EXEC_EMPTY, 0);
    }
    reset_failure();
    for(unsigned fail = 1; fail <= 2; ++fail) {
        vm_exec_load_t *l;
        CHECK(!vm_exec_prepare(e, plan, 2, &l));
        reset_failure(); fail_at = fail;
        CHECK(vm_exec_begin(l) == -1 && errno == ENOMEM);
        exec_info(e, VM_EXEC_FAILED, 0); reset_failure();
        CHECK(!vm_exec_abort(&l) && !l && live == baseline);
    }
    vm_exec_load_t *l;
    exec_watched = e; allocation_inject = exec_outside_lock;
    CHECK(!vm_exec_prepare(e, plan, 2, &l));
    allocation_inject = exec_outside_lock;
    CHECK(!vm_exec_begin(l));
    fail_exec_map = exec_maps + 2;
    CHECK(vm_exec_publish(&l) == -1 && errno == EIO && l);
    CHECK(e->region->pages[0].protection && !e->region->pages[1].protection);
    unsigned held = live; size_t held_bytes = e->info.backing_bytes;
    fail_exec_unmap = exec_unmaps + 2;
    CHECK(vm_exec_abort(&l) == -1 && errno == EIO && l);
    CHECK(exec_info(e, VM_EXEC_CLEANUP, 0).backing_bytes == held_bytes && live == held);
    CHECK(vm_exec_retire(e) == -1 && errno == EBUSY);
    CHECK(vm_exec_destroy(&e) == -1 && errno == EBUSY);
    fail_exec_map = fail_exec_unmap = 0;
    CHECK(!vm_exec_abort(&l) && !l && live == baseline);
    l = exec_writing(e, plan, 2); CHECK(!vm_exec_publish(&l));
    /* Preparation failure while READY preserves both bytes and publication. */
    baseline = live;
    void *old_backing = e->region->pages[0].backing;
    reset_failure(); fail_at = 2;
    vm_exec_load_t *sentinel = (void *)0x1234;
    CHECK(vm_exec_prepare(e, plan, 2, &sentinel) == -1 && errno == ENOMEM);
    CHECK(sentinel == (void *)0x1234 && live == baseline);
    CHECK(e->region->pages[0].backing == old_backing);
    exec_info(e, VM_EXEC_READY, 1); reset_failure();
    /* Actual descriptor ownership corruption must retain, never free or erase
       the foreign PTE. Restore the test mutation before the supported retry. */
    mmupage_t *pte = map_virt(contexts[0], BASE / 4096);
    unsigned physical = pte->physical;
    pte->physical ^= 1;
    CHECK(vm_exec_retire(e) == -1 && errno == EIO && live == baseline);
    CHECK(pte->valid && pte->physical == (physical ^ 1));
    CHECK(exec_info(e, VM_EXEC_CLEANUP, 0).backing_bytes == 8192);
    pte->physical = physical;
    CHECK(!vm_exec_retire(e));
    l = exec_writing(e, plan, 2); CHECK(!vm_exec_publish(&l));
    fail_exec_unmap = exec_unmaps + 1;
    CHECK(vm_exec_retire(e) == -1 && errno == EIO);
    exec_info(e, VM_EXEC_CLEANUP, 0); fail_exec_unmap = 0;
    CHECK(!vm_exec_retire(e));
    l = exec_writing(e, plan, 2);
    CHECK(vm_exec_write(l, SIZE_MAX, NULL, 1) == -1 && errno == EINVAL);
    exec_info(e, VM_EXEC_FAILED, 0);
    CHECK(vm_exec_zero(l, 0, 0) == -1 && errno == EBUSY);
    CHECK(!vm_exec_abort(&l));
    const uint8_t hole[] = {5,0}; l = exec_writing(e, hole, 2);
    CHECK(vm_exec_zero(l, 4095, 2) == -1 && errno == EINVAL);
    CHECK(!vm_exec_abort(&l));
    exec_end(s, &e);
}
static void exec_shared_table(void) {
    vm_space_t *s = exec_space(BASE, 12288);
    vm_exec_region_t *e;
    CHECK(!vm_exec_reserve(s, P(BASE), 8192, &e));
    void *data;
    CHECK(!vm_reserve(s, P(BASE+8192), 4096, &data));
    vm_fault_t *ticket;
    CHECK(!vm_fault_arm(s, data, VM_FAULT_ZERO_COMMIT, VM_READWRITE, &ticket));
    unsigned top = (BASE+8192) / 4096 / MMU_SUB_PAGES;
    mmusubcontext_t *shared = contexts[0]->sub[top];
    const uint8_t plan[] = {5,6};
    vm_exec_load_t *l = exec_writing(e, plan, 2);
    CHECK(!vm_exec_publish(&l) && contexts[0]->sub[top] == shared);
    CHECK(!vm_exec_retire(e) && contexts[0]->sub[top] == shared);
    CHECK(shared->page[(BASE/4096+2)%MMU_SUB_PAGES].blank & MMU_VM_PIN);
    CHECK(!vm_fault_release(ticket));
    CHECK(!vm_release(s, data));
    exec_end(s, &e);
}
static void exec_tests(void) {
    exec_layout_and_data(); exec_context_and_boundary(); exec_failures(); exec_shared_table();
    puts("OVL3 production exec ownership/context/writer/publication/cleanup PASS");
}
