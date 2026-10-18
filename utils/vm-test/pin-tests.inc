/* Actual VM3 manager regression cases, included after the VM2 fixture helpers. */
static void pin_denied(vm_space_t *s, void *v, size_t n,
                        vm_pin_access_t access, int error) {
    vm_pin_t *p = (void *)123;
    unsigned before = live;
    CHECK(vm_pin_acquire(s, v, n, access, &p) == -1 && errno == error);
    CHECK(p == (void *)123 && live == before);
}
static void pin_busy(vm_space_t *s, void *page) {
    busy_ticket(s, page); /* The same whole-range mutation interlocks apply. */
    CHECK(vm_protect(s, page, 4096, VM_READWRITE) < 0 && errno == EBUSY);
}
static void pin_geometry(void) {
    const unsigned orders[3][8] = {{0,1,2,3,4,5,6,7}, {0,2,4,6,1,3,5,7}, {3,2,1,0,4,5,6,7}};
    const size_t edges[] = {0,1,31,32,4095};
    for(unsigned mode = 0; mode < 3; ++mode) {
        backing_pool = aligned_alloc(4096, 8 * 4096); CHECK(backing_pool);
        memcpy(pool_order, orders[mode], sizeof(pool_order)); pool_next = 0;
        mmucontext_t *c = context(0), *other = context(1);
        vm_space_t *s = vm_space_create(c, P(BASE), 5 * 4096);
        void *v; CHECK(s && !vm_reserve(s, NULL, 5 * 4096, &v));
        CHECK(!vm_commit(s, v, 4 * 4096, VM_READWRITE));
        mmu_use_table(other); /* Metadata operations cannot select the pin's context. */
        for(unsigned e = 0; e < sizeof(edges)/sizeof(*edges); ++e) {
            for(unsigned single = 0; single < 2; ++single) {
                size_t begin = edges[e], bytes = single ? 1 : 4 * 4096 - begin - 1;
                vm_pin_t *p; CHECK(!vm_pin_acquire(s, P(BASE + begin), bytes, VM_PIN_WRITE, &p));
                CHECK(mmu_cxt_current == other);
                vm_pin_info_t info; CHECK(!vm_pin_query(p, &info));
                CHECK(info.address == P(BASE + begin) && info.bytes == bytes && info.access == VM_PIN_WRITE);
                CHECK(info.context == c && info.asid == 30 && info.run_count == (single || !mode ? 1 : 4));
                vm_phys_run_t all[4], chunks[2], sentinel[2]; size_t got = 99;
                CHECK(!vm_pin_runs(p, 0, all, 4, &got) && got == info.run_count);
                size_t covered = 0;
                for(size_t i = 0; i < got; ++i) {
                    CHECK(all[i].offset == covered && all[i].bytes);
                    for(size_t b = 0; b < all[i].bytes; ++b) {
                        size_t at = begin + covered + b;
                        uintptr_t expected = ((uintptr_t)s->regions->pages[at / 4096].backing & MEM_AREA_CACHE_MASK) + at % 4096;
                        CHECK(all[i].physical + b == expected);
                    }
                    covered += all[i].bytes;
                }
                CHECK(covered == bytes);
                memset(sentinel, 0xa7, sizeof(sentinel));
                for(size_t cursor = 0; cursor < info.run_count; ++cursor) {
                    memcpy(chunks, sentinel, sizeof(chunks)); got = 99;
                    CHECK(!vm_pin_runs(p, cursor, chunks, 1, &got) && got == 1);
                    CHECK(!memcmp(chunks, all + cursor, sizeof(*chunks)));
                    CHECK(!memcmp(chunks + 1, sentinel + 1, sizeof(*chunks)));
                }
                CHECK(!vm_pin_runs(p, 0, NULL, 0, &got) && got == 0);
                CHECK(!vm_pin_runs(p, info.run_count, chunks, 2, &got) && got == 0);
                got = 99; memcpy(chunks, sentinel, sizeof(chunks));
                CHECK(vm_pin_runs(p, info.run_count + 1, chunks, 2, &got) < 0 && errno == EINVAL);
                CHECK(got == 99 && !memcmp(chunks, sentinel, sizeof(chunks)));
                CHECK(vm_pin_runs(p, 0, chunks, SIZE_MAX, &got) < 0 && errno == EOVERFLOW && got == 99);
                CHECK(vm_pin_runs(p, 0, NULL, 1, &got) < 0 && errno == EINVAL && got == 99);
                CHECK(vm_pin_runs(p, 0, chunks, 1, NULL) < 0 && errno == EINVAL);
                vm_pin_t *second; CHECK(!vm_pin_acquire(s, P(BASE + begin), 1, VM_PIN_READ, &second));
                CHECK(s->pin_count == 2 && s->regions->pages[0].pins == 2);
                pin_busy(s, v);
                CHECK(!vm_pin_release(single ? p : second));
                pin_busy(s, v);
                vm_pin_t *remaining = single ? second : p;
                CHECK(!vm_pin_query(remaining, &info) && s->pin_count == 1);
                CHECK(!vm_pin_release(remaining) && !s->pin_count);
                CHECK(!vm_protect(s, v, 4 * 4096, VM_READWRITE));
                CHECK(mmu_cxt_current == other);
            }
        }
        CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); drop(1);
        CHECK(!live); free(backing_pool); backing_pool = NULL;
    }
}
static void pin_admission(void) {
    mmucontext_t *c = context(0); vm_space_t *s = vm_space_create(c, P(BASE), 5 * 4096);
    void *v, *adj; CHECK(s && !vm_reserve(s, P(BASE), 3 * 4096, &v));
    CHECK(!vm_reserve(s, P(BASE + 3 * 4096), 4096, &adj));
    CHECK(!vm_commit(s, v, 4096, VM_READ));
    pin_denied(NULL, v, 1, VM_PIN_READ, EINVAL);
    pin_denied(s, NULL, 1, VM_PIN_READ, EINVAL);
    pin_denied(s, v, 0, VM_PIN_READ, EINVAL);
    pin_denied(s, P(UINTPTR_MAX - 1), 2, VM_PIN_READ, EOVERFLOW);
    pin_denied(s, P(BASE - 1), 1, VM_PIN_READ, EINVAL);
    pin_denied(s, v, 1, 0, ENOTSUP);
    pin_denied(s, P(BASE + 4 * 4096), 1, VM_PIN_READ, ENOENT);
    pin_denied(s, v, 3 * 4096 + 1, VM_PIN_READ, EINVAL);
    pin_denied(s, v, 1, VM_PIN_WRITE, EACCES);
    pin_denied(s, v, 1, VM_PIN_READWRITE, EACCES);
    pin_denied(s, v, 4097, VM_PIN_WRITE, ENOENT); /* Hole precedes permission. */
    vm_pin_t *p; CHECK(vm_pin_acquire(s, v, 1, VM_PIN_READ, NULL) < 0 && errno == EINVAL);
    vm_fault_t *t; CHECK(!vm_fault_arm(s, P(BASE + 4096), VM_FAULT_ZERO_COMMIT, VM_READ, &t));
    pin_denied(s, v, 3 * 4096, VM_PIN_WRITE, EBUSY); /* Ticket precedes hole/permission. */
    CHECK(!vm_fault_release(t));
    CHECK(!vm_commit(s, P(BASE + 4096), 2 * 4096, VM_READWRITE));
    for(unsigned page = 0; page < 3; ++page) {
        s->regions->pages[page].pins = UINT32_MAX;
        pin_denied(s, v, 3 * 4096, VM_PIN_READ, EOVERFLOW);
        for(unsigned i = 0; i < 3; ++i) CHECK(s->regions->pages[i].pins == (i == page ? UINT32_MAX : 0));
        CHECK(!s->pin_count); s->regions->pages[page].pins = 0;
    }
    s->pin_count = UINT32_MAX; pin_denied(s, v, 1, VM_PIN_READ, EOVERFLOW); s->pin_count = 0;
    reset_failure(); fail_at = 1; pin_denied(s, v, 3 * 4096, VM_PIN_READ, ENOMEM); reset_failure();
    for(unsigned i = 0; i < 3; ++i) CHECK(!s->regions->pages[i].pins);
    fail_lock = 1; pin_denied(s, v, 1, VM_PIN_READ, EAGAIN); fail_lock = 0;
    CHECK(!vm_pin_acquire(s, v, 1, VM_PIN_READ, &p));
    vm_pin_info_t info, old; memset(&old, 0x91, sizeof(old)); info = old;
    size_t n = 99; vm_phys_run_t run = {1,2,3}, previous = run;
    for(unsigned i = 0; i < 4; ++i) {
        test_irq = i == 0; test_sr = i == 1 ? 0x10000000u : i == 2 ? 0x10u : 0xf0u;
        pin_denied(s, v, 1, VM_PIN_READ, EPERM);
        CHECK(vm_pin_query(p, &info) < 0 && errno == EPERM);
        CHECK(vm_pin_runs(p, 0, &run, 1, &n) < 0 && errno == EPERM);
        CHECK(vm_pin_release(p) < 0 && errno == EPERM);
        CHECK(!memcmp(&info, &old, sizeof(info)) && !memcmp(&run, &previous, sizeof(run)) && n == 99);
        CHECK(s->pin_count == 1 && s->regions->pages[0].pins == 1);
    }
    test_sr = 0; test_irq = 0; fail_lock = 1;
    CHECK(vm_pin_query(p, &info) < 0 && errno == EAGAIN);
    CHECK(vm_pin_runs(p, 0, &run, 1, &n) < 0 && errno == EAGAIN);
    CHECK(vm_pin_release(p) < 0 && errno == EAGAIN);
    CHECK(!memcmp(&info, &old, sizeof(info)) && !memcmp(&run, &previous, sizeof(run)) && n == 99);
    fail_lock = 0;
    CHECK(vm_pin_query(NULL, &info) < 0 && errno == EINVAL);
    CHECK(vm_pin_query(p, NULL) < 0 && errno == EINVAL);
    CHECK(vm_pin_runs(NULL, 0, &run, 1, &n) < 0 && errno == EINVAL);
    CHECK(vm_pin_release(NULL) < 0 && errno == EINVAL);
    CHECK(vm_decommit(s, v, 3 * 4096) < 0 && errno == EBUSY);
    CHECK(s->regions->pages[1].backing && s->regions->pages[2].backing);
    CHECK(!vm_decommit(s, P(BASE + 4096), 4096)); /* unrelated page */
    CHECK(!vm_pin_release(p));
    CHECK(!vm_release(s, v) && !vm_release(s, adj) && !vm_space_destroy(s)); drop(0); CHECK(!live);
}
static vm_space_t *pin_fault_space;
static vm_fault_t *pin_fault_ticket;
static void pin_interrupt(void) {
    irq_context_t f = fault_frame();
    CHECK(resolve(pin_fault_space, &f, 0x60,
                  (uintptr_t)pin_fault_ticket->info.page + 28,
                  pin_fault_space->context->asid, 1));
}
static void pin_tickets(void) {
    for(unsigned guard = 0; guard < 2; ++guard) {
        for(unsigned resolved = 0; resolved < 2; ++resolved) {
            mmucontext_t *c = context(0); mmu_use_table(c);
            vm_space_t *s = vm_space_create(c, P(BASE), 2 * 4096);
            void *v; vm_fault_t *t; vm_pin_t *p;
            CHECK(s && !vm_reserve(s, NULL, 2 * 4096, &v));
            CHECK(!vm_commit(s, P(BASE + 4096), 4096, VM_READWRITE));
            if(guard) CHECK(!vm_commit(s, v, 4096, VM_READWRITE));
            CHECK(!vm_fault_arm(s, v, guard ? VM_FAULT_GUARD : VM_FAULT_ZERO_COMMIT, VM_READWRITE, &t));
            if(resolved) CHECK(resolve_ok(s, t, 0x60));
            pin_denied(s, v, 1, VM_PIN_READ, EBUSY);
            if(!resolved) { pin_fault_space = s; pin_fault_ticket = t; allocation_inject = pin_interrupt; }
            CHECK(!vm_pin_acquire(s, P(BASE + 4097), 1, VM_PIN_READWRITE, &p));
            CHECK(!allocation_inject && t->info.state == VM_FAULT_RESOLVED);
            pin_denied(s, v, 1, VM_PIN_READ, EBUSY);
            CHECK(!vm_fault_release(t));
            vm_pin_t *q; CHECK(!vm_pin_acquire(s, v, 1, VM_PIN_READWRITE, &q));
            CHECK(!vm_pin_release(q) && !vm_pin_release(p));
            CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
        }
    }
}
struct pin_borrower { vm_pin_t *pin; atomic_bool done; };
static void *pin_enumerator(void *arg) {
    struct pin_borrower *b = arg; vm_pin_info_t i; vm_phys_run_t r; size_t n;
    CHECK(!vm_pin_query(b->pin, &i));
    CHECK(!vm_pin_runs(b->pin, 0, &r, 1, &n) && n == 1 && r.bytes == i.bytes);
    atomic_store(&b->done, true); return NULL;
}
struct pin_consumer {
    vm_pin_t *pin;
    unsigned char *byte;
    unsigned char value;
    atomic_bool ready, resume, retired;
};
static void *pin_delayed_consumer(void *arg) {
    struct pin_consumer *c = arg;
    vm_phys_run_t run; size_t n;
    CHECK(!vm_pin_runs(c->pin, 0, &run, 1, &n) && n == 1);
    atomic_store(&c->ready, true);
    while(!atomic_load(&c->resume)) sched_yield();
    /* Host substitute for the stage's final buffer use, not real device access. */
    *c->byte = c->value;
    atomic_store(&c->retired, true);
    return NULL;
}
static void *pin_reaper(void *arg) { CHECK(!vm_pin_release(arg)); return NULL; }
static void pin_retirement(void) {
    vm_space_t *s = vm_space_create(context(0), P(BASE), 4096);
    void *v; vm_pin_t *p; CHECK(s && !vm_reserve(s, NULL, 4096, &v));
    CHECK(!vm_commit(s, v, 4096, VM_READWRITE));
    CHECK(!vm_pin_acquire(s, P(BASE + 1), 32, VM_PIN_READWRITE, &p));
    struct pin_borrower b = {.pin=p}; pthread_t reader, reaper;
    CHECK(!mutex_lock(&s->mutex)); watched_mutex = &s->mutex; atomic_store(&mutex_waiters, 0);
    CHECK(!pthread_create(&reader, NULL, pin_enumerator, &b));
    while(!atomic_load(&mutex_waiters)) sched_yield();
    CHECK(!atomic_load(&b.done) && s->pin_count == 1);
    CHECK(!mutex_unlock(&s->mutex)); CHECK(!pthread_join(reader, NULL));
    watched_mutex = NULL; CHECK(atomic_load(&b.done));
    /* Six separately paused consumers model worker, DMA, IRQ, finalizer,
       callback and suspended continuation. Status/cancel/timeout notifications
       do not join any of them; each still performs its final buffer use. */
    struct pin_consumer consumers[6] = {0}; pthread_t users[6];
    unsigned char *backing = s->regions->pages[0].backing;
    for(unsigned i = 0; i < 6; ++i) {
        consumers[i].pin = p; consumers[i].byte = backing + 1 + i;
        consumers[i].value = 0x70 + i;
        CHECK(!pthread_create(&users[i], NULL, pin_delayed_consumer, &consumers[i]));
        while(!atomic_load(&consumers[i].ready)) sched_yield();
    }
    for(unsigned event = 0; event < 3; ++event) { /* cancel, wait timeout, terminal wake */
        pin_busy(s, v);
        for(unsigned i = 0; i < 6; ++i)
            CHECK(!atomic_load(&consumers[i].retired) && !backing[1 + i]);
    }
    for(unsigned i = 0; i < 6; ++i) {
        pin_busy(s, v); CHECK(s->pin_count == 1);
        atomic_store(&consumers[i].resume, true);
        CHECK(!pthread_join(users[i], NULL));
        CHECK(atomic_load(&consumers[i].retired) && backing[1 + i] == 0x70 + i);
        for(unsigned j = i + 1; j < 6; ++j) CHECK(!atomic_load(&consumers[j].retired));
    }
    /* The resident reaper receives ownership after publication/acknowledgement
       and all borrowers/consumers are joined. It need not be acquisition thread. */
    CHECK(!pthread_create(&reaper, NULL, pin_reaper, p)); CHECK(!pthread_join(reaper, NULL));
    CHECK(!s->pin_count && !s->regions->pages[0].pins);
    CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
}
static void pin_tests(void) {
    pin_geometry(); pin_admission(); pin_tickets(); pin_retirement();
    puts("VM3 production pin geometry/admission/tickets/retirement PASS");
}
