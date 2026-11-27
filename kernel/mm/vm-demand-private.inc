/* Included only by vm.c under the explicit experimental build flag. */
struct vm_demand {
    vm_file_t *file;
    kthread_t *carrier;
    mmucontext_t *context;
    uintptr_t low;
    size_t stack_bytes, headroom, first, count, page;
    unsigned asid, slot;
    uint32_t fpscr, event, address, pteh, mmucr;
    int *errno_location, saved_errno;
    vm_demand_state_t state;
    irq_context_t saved;
};
static vm_demand_t *demand_slots[VM_DP1_USERS];

static uintptr_t demand_sp(void) {
#ifdef KOS_VM_DP1_HOST_TEST
    return test_demand_sp;
#else
    uintptr_t sp; __asm__ volatile("mov r15,%0" : "=r"(sp)); return sp;
#endif
}
static uint32_t demand_fpscr(void) {
#ifdef KOS_VM_DP1_HOST_TEST
    return 0x40000;
#else
    uint32_t value; __asm__ volatile("sts fpscr,%0" : "=r"(value)); return value;
#endif
}
static bool demand_stack(vm_demand_t *u, uintptr_t raw) {
    if((raw & 3) || raw < u->low || raw-u->low > u->stack_bytes)
        return false;
    uintptr_t aligned = raw & ~(uintptr_t)7;
    return aligned >= u->low && aligned-u->low >= u->headroom;
}
static void demand_error(vm_file_t *f, int error) {
    irq_disable_scoped();
    if(f->demand_uses && !f->demand_error)
        f->demand_error = error ? error : EIO;
}
static int demand_error_value(vm_file_t *f) {
    irq_disable_scoped();
    return f->demand_error;
}
static int demand_admission_error(vm_file_t *f) {
    irq_disable_scoped();
    if(f->demand_error)
        return f->demand_error;
    return f->info.cancelled ? ECANCELED : 0;
}
static void demand_complete(vm_file_t *f) {
    irq_disable_scoped();
    ++f->demand_generation;
    genwait_wake_all(&f->demand_generation);
}
/* Called with space locked. Count each reserved/ready/loading page once. */
static bool demand_page_occupied(vm_file_t *f, size_t page) {
    vm_page_t *p = &f->region->pages[page];
    return p->protection || p->demand_refs ||
           (f->info.loading_pages && f->loading_page == page);
}

static size_t demand_occupied(vm_file_t *f) {
    size_t occupied = 0;
    for(size_t i = 0; i < f->info.pages; ++i)
        if(demand_page_occupied(f, i))
            ++occupied;
    return occupied;
}

static bool demand_page_reclaimable(vm_file_t *f, size_t page) {
    return f->region->pages[page].protection == VM_READ &&
           !vm_file_page_busy(f, page);
}

static bool demand_plan_reclaim(vm_file_t *f, size_t first, size_t count,
                                size_t *reclaim) {
    size_t occupied = count;
    size_t available = 0;
    for(size_t i = 0; i < f->info.pages; ++i) {
        if((i >= first && i < first + count) || !demand_page_occupied(f, i))
            continue;
        ++occupied;
        if(demand_page_reclaimable(f, i))
            ++available;
    }
    size_t limit = f->info.resident_limit;
    *reclaim = occupied > limit ? occupied - limit : 0;
    return available >= *reclaim;
}

int vm_demand_begin(vm_file_t *f, size_t first, size_t count,
                    const vm_demand_profile_t *profile, vm_demand_t **result) {
    if(vm_caller() < 0) return -1;
    if(!f || !result || !profile || !profile->closed_test || !profile->headroom ||
       !count || first >= f->info.pages || count > f->info.pages-first)
        return vm_error(EINVAL);
    int error = demand_admission_error(f);
    if(error)
        return vm_error(error);
#ifndef KOS_VM_DP1_HOST_TEST
    if(fs_iso9660_validate_demand_source(f->fd) < 0)
        return -1;
#endif
    uintptr_t raw = demand_sp(), low = 0; size_t bytes = 0;
    if(!thd_current) return vm_error(ENOTSUP);
    if(!_thd_continuation_stack_bounds(thd_current, raw, &low, &bytes)) {
        low = (uintptr_t)thd_current->stack; bytes = thd_current->stack_size;
    }
    if(!bytes || bytes > UINTPTR_MAX-low || !vm_direct(low) ||
       !vm_direct(low+bytes-1)) return vm_error(ENOTSUP);
    if((raw & 3) || raw < low || raw-low > bytes ||
       (raw & ~(uintptr_t)7) < low ||
       (raw & ~(uintptr_t)7)-low < profile->headroom) return vm_error(ENOSPC);
    if(mmu_vm_context_matches(f->space->context, f->space->context->asid) < 0) return -1;
    vm_demand_t *u = calloc(1, sizeof(*u));
    if(!u) {
        error = demand_admission_error(f);
        return vm_error(error ? error : ENOMEM);
    }
    /* Initialize fields directly: no full saved-frame-sized admission local. */
    u->file=f; u->carrier=thd_current; u->context=f->space->context;
    u->low=low; u->stack_bytes=bytes; u->headroom=profile->headroom;
    u->first=first; u->count=count; u->asid=f->space->context->asid;
    u->fpscr=demand_fpscr(); u->errno_location=&errno; u->state=VM_DP1_IDLE;
    if(mutex_lock(&f->space->mutex) < 0) { free(u); return -1; }
    error = demand_admission_error(f);
    size_t reclaim = 0;
    size_t reserve = f->info.fixed_bytes + f->info.table_reserve_bytes +
                     f->info.resident_limit * VM_PAGE_BYTES;
    if(!error && (f->demand_bytes > f->info.budget_bytes-reserve ||
       sizeof(*u) > f->info.budget_bytes-reserve-f->demand_bytes ||
       !demand_plan_reclaim(f, first, count, &reclaim))) error=EAGAIN;
    {
        irq_disable_scoped();
        if(f->demand_error) error=f->demand_error;
        unsigned slot=VM_DP1_USERS;
        for(unsigned i=0; i<VM_DP1_USERS; ++i) {
            if(!error && demand_slots[i] && demand_slots[i]->carrier==thd_current) error=EBUSY;
            if(!demand_slots[i] && slot==VM_DP1_USERS) slot=i;
        }
        if(!error && slot==VM_DP1_USERS) error=EAGAIN;
        if(!error) {
            u->slot=slot;
            for(size_t i=first; i<first+count; ++i) ++f->region->pages[i].demand_refs;
            ++f->demand_uses; f->demand_bytes+=sizeof(*u);
            f->info.charged_bytes+=sizeof(*u); demand_slots[slot]=u;
        }
    }
    /* Admission is committed. The space lock keeps the plan stable and the
       new references exclude the requested range. Retire each victim before
       unlocking, with no outer IRQ mask across the cache/TLB work. */
    if(!error) {
        for(size_t i = 0; reclaim && i < f->info.pages; ++i) {
            if(demand_page_reclaimable(f, i)) {
                vm_file_evict_locked(f, i);
                --reclaim;
            }
        }
        assert(!reclaim);
    }
    mutex_unlock(&f->space->mutex);
    if(error) { free(u); return vm_error(error); }
    *result=u; return 0;
}
int vm_demand_end(vm_demand_t *u) {
    if(vm_caller()<0) return -1;
    if(!u) return vm_error(EINVAL);
    vm_file_t *f=u->file;
    if(mutex_lock(&f->space->mutex)<0) return -1;
    bool busy;
    {
        irq_disable_scoped();
        busy=u->state!=VM_DP1_IDLE || f->info.loading_pages;
        if(!busy) {
            demand_slots[u->slot]=NULL;
            for(size_t i=u->first; i<u->first+u->count; ++i) --f->region->pages[i].demand_refs;
            --f->demand_uses; f->demand_bytes-=sizeof(*u);
            f->info.charged_bytes-=sizeof(*u);
        }
    }
    mutex_unlock(&f->space->mutex);
    if(busy) return vm_error(EBUSY);
    free(u); return 0;
}
int vm_file_demand_begin(vm_file_t *f, size_t first, size_t count,
                         vm_file_demand_t **out) {
    static const vm_demand_profile_t profile = {
        .headroom = 8192, .debug_bytes = 56, .closed_test = true
    };
    if(vm_caller() < 0)
        return -1;
    if(!thd_current || _thd_continuation_stack_bounds(thd_current, demand_sp(), NULL, NULL))
        return vm_error(ENOTSUP);
    return vm_demand_begin(f, first, count, &profile, out);
}

int vm_file_demand_end(vm_file_demand_t *u) {
    if(vm_caller() < 0)
        return -1;
    if(!u)
        return vm_error(EINVAL);
    uintptr_t sp = demand_sp();
    if(u->carrier != thd_current || sp < u->low || sp - u->low > u->stack_bytes)
        return vm_error(EPERM);
    return vm_demand_end(u);
}

bool vm_demand_defer(mmucontext_t *c, irq_t event, irq_context_t *frame,
                     uint32_t address, uint32_t pteh_value, uint32_t control) {
    /* Caller already excludes IRQs. No locks, allocation or provider lookup. */
    if(!frame || !c || event!=EXC_DTLB_MISS_READ || c!=mmu_cxt_current ||
       c->asid<0 || c->asid>255 || (control&0x101u)!=1 ||
       (pteh_value&255)!=(unsigned)c->asid ||
       (frame->sr&0x700000f0u)!=0x40000000u ||
       !vm_direct(frame->pc) || (frame->pc&1) || !vm_direct(frame->r[15])) return false;
    for(unsigned i=0; i<VM_DP1_USERS; ++i) {
        vm_demand_t *u=demand_slots[i];
        if(!u || u->carrier!=thd_current || u->context!=c || u->asid!=(unsigned)c->asid ||
           u->state!=VM_DP1_IDLE || !demand_stack(u,frame->r[15])) continue;
        uintptr_t base=u->file->region->base+u->first*VM_PAGE_BYTES;
        if(address<base || address-base>=u->count*VM_PAGE_BYTES) continue;
        size_t page=(address-u->file->region->base)/VM_PAGE_BYTES;
        if(u->file->region->pages[page].protection) return false;
        u->saved=*frame; u->saved_errno=*u->errno_location;
        u->event=event; u->address=address; u->pteh=pteh_value; u->mmucr=control;
        u->page=page; u->state=VM_DP1_SAVED;
        frame->pc=(uintptr_t)vm_demand_recover; frame->r[4]=(uintptr_t)u;
        frame->r[15]&=~7u; frame->sr=0x40000000u; frame->fpscr=u->fpscr;
        return true;
    }
    return false;
}
int vm_demand_prepare(vm_demand_t *u) {
    if(vm_caller()<0) return -1;
    if(!u || u->carrier!=thd_current || u->state==VM_DP1_IDLE) return vm_error(EINVAL);
    vm_file_t *f=u->file;
    if(mmu_vm_context_matches(u->context,u->asid)<0) demand_error(f,EBUSY);
    for(;;) {
        if(mutex_lock(&f->space->mutex)<0) return -1;
        uint32_t generation; bool wait=false, ready=false; int error;
        {
            irq_disable_scoped();
            error=f->demand_error;
            generation=f->demand_generation;
            if(f->info.loading_pages) { u->state=VM_DP1_WAITING; wait=true; }
            else if(error) u->state=VM_DP1_FAILED;
            else if(f->region->pages[u->page].protection) { u->state=VM_DP1_RETRY; ready=true; }
            else u->state=VM_DP1_LOADING;
        }
        mutex_unlock(&f->space->mutex);
        if(wait) {
            irq_disable_scoped();
            if(f->demand_generation==generation)
                genwait_wait(&f->demand_generation,"vm demand load",0);
            continue;
        }
        if(error) return vm_error(error);
        if(ready) return 0;
        if(vm_file_prefetch(f,u->page)<0 && errno!=EBUSY) demand_error(f,errno);
    }
}
bool vm_demand_terminal_locked(vm_demand_t *u) {
    return u && u->state==VM_DP1_FAILED && u->file->demand_error &&
           !u->file->info.loading_pages;
}
bool vm_demand_retry_locked(vm_demand_t *u, irq_context_t **saved,
                            irq_context_t **destination) {
    if(!u || u->state!=VM_DP1_RETRY) return false;
    if(u->carrier!=thd_current || mmu_vm_context_matches(u->context,u->asid)<0)
        demand_error(u->file,EBUSY);
    if(u->file->demand_error || u->file->info.loading_pages) return false;
    *u->errno_location=u->saved_errno;
    *saved=&u->saved; *destination=&u->carrier->context;
    u->state=VM_DP1_IDLE;
    return true;
}
void vm_demand_recover(vm_demand_t *u) {
    for(;;) {
        int rc=vm_demand_prepare(u);
        irq_mask_t mask=irq_disable();
        if(rc<0 && vm_demand_terminal_locked(u)) vm_demand_stop();
        irq_context_t *saved,*destination;
        if(!rc && vm_demand_retry_locked(u,&saved,&destination))
            vm_demand_return(saved,destination);
        irq_restore(mask);
    }
}
