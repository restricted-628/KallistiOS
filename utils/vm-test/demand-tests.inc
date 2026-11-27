/* Actual VM/VFS/DP1 code, with host scheduling/register/backend substitutes. */
#include <unistd.h>
#include <sys/wait.h>
static kthread_t demand_carriers[9];
static irq_context_t demand_original[9];
static const vm_demand_profile_t demand_profile={32768,0,0,true};
static vm_demand_t *demand_users[2];
static vm_file_t *demand_other;
static unsigned demand_cleanup_checks;
static atomic_bool demand_hold_cleanup, demand_cleanup_entered, demand_cleanup_resume;
void vm_demand_stop(void) { CHECK(false); abort(); }
void vm_demand_return(irq_context_t *s,irq_context_t *d) {
    (void)s;(void)d;CHECK(false);abort();
}
static void demand_select(unsigned i) {
    thd_current=&demand_carriers[i];
    test_demand_sp=(uintptr_t)thd_current->stack+thd_current->stack_size-256;
}
static void demand_page_check(const unsigned char *p,size_t n) {
    CHECK(n==4096 && file_under_test->info.loading_pages);
    size_t page=file_under_test->loading_page;
    size_t valid=vm_file_valid_bytes(file_under_test,page);
    size_t offset=file_under_test->info.offset+page*4096;
    for(size_t i=0;i<n;++i) CHECK(p[i]==(i<valid?source_byte(offset+i):0));
}
static vm_file_t *demand_setup(void) {
    file_setup(); map_func=map_virt; mmucr_value=1;
    mmu_use_table(contexts[0]);mmu_switch_context(contexts[0]);
    for(unsigned i=0;i<9;++i)
        demand_carriers[i]=(kthread_t){.stack=P(0x8c200000+i*0x20000),.stack_size=65536};
    demand_select(0);check_page=demand_page_check;
    file_under_test=file_open_options(file_options());return file_under_test;
}
static vm_demand_t *demand_begin_test(vm_file_t *f,unsigned carrier,size_t first,size_t count) {
    demand_select(carrier);vm_demand_t *u=NULL;
    CHECK(!vm_demand_begin(f,first,count,&demand_profile,&u));return u;
}
static void demand_save(vm_demand_t *u,unsigned i,size_t page) {
    demand_select(i);irq_context_t frame={.pc=0x8c010000,.sr=0x40000000,.fpscr=0x2c0001};
    frame.r[15]=test_demand_sp;frame.r[6]=0x12345678;demand_original[i]=frame;errno=EDOM;
    irq_disable_scoped();
    CHECK(vm_demand_defer(contexts[0],0x40,&frame,(uintptr_t)u->file->info.address+page*4096,
                         contexts[0]->asid,1));
    CHECK(u->state==VM_DP1_SAVED && !memcmp(&u->saved,&demand_original[i],sizeof(frame)));
    CHECK(frame.pc!=(uintptr_t)demand_original[i].pc && u->saved_errno==EDOM);
}
static void demand_retry(vm_demand_t *u,unsigned i) {
    demand_select(i);irq_context_t *s=NULL,*d=NULL;
    irq_disable_scoped();
    CHECK(vm_demand_retry_locked(u,&s,&d) && u->state==VM_DP1_IDLE);
    CHECK(s==&u->saved && d==&u->carrier->context && errno==EDOM);
    CHECK(!memcmp(s,&demand_original[i],sizeof(*s)));
}
static void demand_refusal(vm_file_t *f,vm_demand_t *u) {
    int close_error=19;
    CHECK(vm_demand_end(u)<0 && errno==EBUSY);
    CHECK(vm_file_close(f,&close_error)<0 && errno==EBUSY && close_error==19);
    CHECK(vm_file_evict(f,u->page)<0 && errno==EBUSY);
    CHECK(vm_space_destroy(file_space)<0 && errno==EBUSY);
    CHECK(vm_release(file_space,(void *)f->info.address)<0 && errno==EBUSY);
}
typedef struct {vm_demand_t *u;unsigned carrier;int rc,error;} demand_worker_t;
static bool demand_gap;
static atomic_bool demand_first_done, demand_gap_seen, demand_seek_park;
static void demand_before_enqueue(void *mutex) {
    if(mutex!=&file_under_test->space->mutex ||
       demand_users[1]->state!=VM_DP1_WAITING) return;
    mutex_unlock_inject=NULL;
    CHECK(!irq_depth);
    atomic_store(&read_resume,true);
    while(!atomic_load(&demand_first_done)) sched_yield();
    atomic_store(&demand_gap_seen,true);
}
static void demand_delayed_seek(void) {
    file_callback_locks();
    /* Installed only after open: the next backend operation is actual seek. */
    if(atomic_exchange(&demand_seek_park,false)) {
        atomic_store(&read_entered,true);
        while(!atomic_load(&read_resume)) sched_yield();
    }
}
static void *demand_worker(void *arg) {
    demand_worker_t *w=arg;demand_select(w->carrier);
    if(demand_gap && w->carrier==1) mutex_unlock_inject=demand_before_enqueue;
    w->rc=vm_demand_prepare(w->u);w->error=errno;
    if(!w->carrier) atomic_store(&demand_first_done,true);
    return NULL;
}
static void demand_success(unsigned mode) {
    bool different=mode==1;
    vm_file_t *f=demand_setup();size_t charge=f->info.charged_bytes;
    vm_demand_t *a=demand_begin_test(f,0,0,1);
    vm_demand_t *b=demand_begin_test(f,1,different?1:0,1);
    demand_users[0]=a;demand_users[1]=b;demand_gap=mode==2;
    atomic_store(&demand_first_done,false);atomic_store(&demand_gap_seen,false);
    CHECK(f->demand_uses==2 && f->demand_bytes==2*sizeof(*a));
    CHECK(f->info.charged_bytes==charge+2*sizeof(*a));
    CHECK(f->region->pages[0].demand_refs==(different?1u:2u));
    /* The reserved union consumes capacity; unrelated prefetch cannot steal it. */
    if(different) CHECK(vm_file_prefetch(f,2)<0 && errno==EAGAIN);
    demand_save(a,0,0);demand_save(b,1,different?1:0);
    demand_refusal(f,a);
    unsigned reads=source_reads,waits=atomic_load(&demand_waits);
    atomic_store(&read_park,mode!=3);atomic_store(&read_entered,false);atomic_store(&read_resume,false);
    if(mode==3) {backend_check=demand_delayed_seek;atomic_store(&demand_seek_park,true);}
    demand_worker_t wa={a,0,0,0},wb={b,1,0,0};pthread_t ta,tb;
    CHECK(!pthread_create(&ta,NULL,demand_worker,&wa));
    while(!atomic_load(&read_entered)) sched_yield();
    CHECK(!pthread_create(&tb,NULL,demand_worker,&wb));
    if(!demand_gap) {
        while(atomic_load(&demand_waits)==waits) sched_yield();
        demand_refusal(f,a);demand_refusal(f,b);
        if(different) {
            vm_file_demand_t *out = NULL;
            demand_select(2);
            CHECK(vm_file_demand_begin(f, 2, 1, &out) < 0 && errno == EAGAIN && !out);
            CHECK(f->info.loading_pages == 1 && f->loading_page == 0);
        }
        {irq_disable_scoped();genwait_wake_all(&f->demand_generation);}
        while(atomic_load(&demand_waits)==waits+1) sched_yield();
        CHECK(source_reads==reads+(mode==3?0u:1u) && f->info.loading_pages==1);
        atomic_store(&read_resume,true);
    }
    CHECK(!pthread_join(ta,NULL));CHECK(!pthread_join(tb,NULL));
    if(demand_gap) CHECK(atomic_load(&demand_gap_seen) && atomic_load(&demand_waits)==waits);
    demand_gap=false;
    atomic_store(&read_park,false);
    CHECK(!wa.rc && !wb.rc && source_reads==reads+(different?2u:1u));
    demand_refusal(f,a); /* RETRY is still a real retained owner. */
    vm_pin_t *pin=NULL;
    CHECK(!vm_pin_acquire(file_space,f->info.address,4096,VM_PIN_READ,&pin));
    demand_retry(a,0);demand_retry(b,1);
    CHECK(!vm_demand_end(a));CHECK(!vm_demand_end(b));
    CHECK(!f->demand_uses && !f->demand_bytes);
    CHECK(vm_file_evict(f,0)<0 && errno==EBUSY);
    CHECK(!vm_pin_release(pin));CHECK(!vm_file_evict(f,0));
    if(different) CHECK(!vm_file_evict(f,1));
    CHECK(f->info.charged_bytes==charge);
    /* Rebegin/reload checks ordinary successful quiescence, not just refusal. */
    a=demand_begin_test(f,0,2,1);demand_save(a,0,2);
    CHECK(!vm_demand_prepare(a));demand_retry(a,0);CHECK(!vm_demand_end(a));
    CHECK(!vm_file_evict(f,2));
    int fd=f->fd,error=0;unsigned closes=source_closes;
    source_close_error=EIO;
    CHECK(!vm_file_close(f,&error) && error==EIO && source_closes==closes+1 && !fd_table[fd]);
    source_close_error=0;
    int reused=fs_open("/source/data",O_RDONLY);CHECK(reused==fd);CHECK(!fs_close(reused));
    file_end(NULL);file_under_test=NULL;
}
static void demand_reclaim(void) {
    vm_file_t *f = demand_setup();
    int close_error;
    CHECK(!vm_file_close(f, &close_error) && !close_error);
    vm_file_options_t o = file_options();
    o.bytes += 4096;
    f = file_under_test = file_open_options(o);
    size_t fixed_charge = f->info.charged_bytes;
    CHECK(!vm_file_prefetch(f, 0) && !vm_file_prefetch(f, 1));
    CHECK(f->info.resident_pages == 2);
    unsigned reads = source_reads;
    unsigned retired = invalidations;
    vm_pin_t *pin = NULL;
    CHECK(!vm_pin_acquire(file_space, f->info.address, 4096, VM_PIN_READ, &pin));
    vm_file_demand_t *use = NULL;
    /* A partial reclaim plan must leave both pages resident. */
    CHECK(vm_file_demand_begin(f, 2, 2, &use) < 0 && errno == EAGAIN && !use);
    CHECK(vm_file_demand_begin(f, 1, 2, &use) < 0 && errno == EAGAIN && !use);
    CHECK(f->info.resident_pages == 2 && source_reads == reads && invalidations == retired);
    CHECK(!vm_pin_release(pin));

    CHECK(!vm_file_demand_begin(f, 1, 2, &use));
    CHECK(f->info.resident_pages == 1 && !f->info.loading_pages);
    CHECK(f->info.charged_bytes == fixed_charge + 4096 + sizeof(*use));
    vm_file_page_info_t info;
    CHECK(!vm_file_page_query(f, 0, &info) && info.state == VM_FILE_ABSENT);
    CHECK(!vm_file_page_query(f, 1, &info) && info.state == VM_FILE_READY);
    CHECK(!vm_file_page_query(f, 2, &info) && info.state == VM_FILE_ABSENT);
    CHECK(source_reads == reads && invalidations > retired);

    vm_file_demand_t *other = NULL;
    demand_select(1);
    CHECK(vm_file_demand_begin(f, 0, 1, &other) < 0 && errno == EAGAIN && !other);
    demand_save(use, 0, 2);
    CHECK(!vm_demand_prepare(use));
    demand_retry(use, 0);
    CHECK(!vm_file_demand_end(use) && f->info.resident_pages == 2);

    CHECK(!vm_file_demand_begin(f, 2, 1, &use));
    retired = invalidations;
    CHECK(vm_file_demand_begin(f, 0, 1, &other) < 0 && errno == EBUSY && !other);
    CHECK(f->info.resident_pages == 2 && invalidations == retired);
    CHECK(!vm_file_demand_end(use));
    CHECK(!vm_file_demand_begin(f, 0, 1, &use));
    CHECK(!vm_file_page_query(f, 0, &info) && info.state == VM_FILE_ABSENT);
    CHECK(source_reads == reads + 1);
    demand_save(use, 0, 0);
    CHECK(!vm_demand_prepare(use));
    demand_retry(use, 0);
    CHECK(source_reads == reads + 2 && f->info.resident_pages == 2);
    CHECK(!vm_file_demand_end(use));
    CHECK(f->info.charged_bytes == fixed_charge + 2 * 4096);
    CHECK(!vm_file_evict(f, 2));
    CHECK(!vm_file_prefetch(f, 1));
    CHECK(!vm_file_demand_begin(f, 2, 2, &use));
    CHECK(!f->info.resident_pages && !f->info.loading_pages);
    CHECK(f->info.charged_bytes == fixed_charge + sizeof(*use));
    CHECK(!vm_file_demand_end(use));
    CHECK(f->info.charged_bytes == fixed_charge);
    file_end(f);
    file_under_test = NULL;
}

static void demand_admission(void) {
    vm_file_t *f=demand_setup();vm_demand_t *u[8],*out=(void *)1;
    demand_select(0);
    test_child_stack=true;
    CHECK(vm_file_demand_begin(f,0,1,&out)<0 && errno==ENOTSUP && out==(void *)1);
    test_child_stack=false;
    CHECK(!vm_file_demand_begin(f,0,1,&out));
    demand_select(1);
    CHECK(vm_file_demand_end(out)<0 && errno==EPERM && f->demand_uses==1);
    demand_select(0);
    CHECK(!vm_file_demand_end(out) && f->demand_uses==0);
    out=(void *)1;
    for(unsigned i=0;i<8;++i) u[i]=demand_begin_test(f,i,0,1);
    unsigned reads=source_reads;demand_select(8);
    CHECK(vm_demand_begin(f,0,1,&demand_profile,&out)<0 && errno==EAGAIN && out==(void *)1);
    demand_select(0);
    CHECK(vm_demand_begin(f,0,1,&demand_profile,&out)<0 && errno==EBUSY);
    CHECK(vm_file_evict(f,0)<0 && errno==EBUSY && source_reads==reads);
    CHECK(f->region->pages[0].demand_refs==8);
    for(unsigned i=0;i<8;++i) CHECK(!vm_demand_end(u[i]));
    CHECK(source_reads==reads);file_end(f);
    f=demand_setup();size_t envelope=f->info.charged_bytes+4096*f->info.resident_limit;
    int e;CHECK(!vm_file_close(f,&e));
    vm_file_options_t o=file_options();o.budget_bytes=envelope;
    f=file_open_options(o);demand_select(0);
    CHECK(vm_demand_begin(f,0,1,&demand_profile,&out)<0 && errno==EAGAIN);
    CHECK(!f->demand_uses && !f->demand_error);
    CHECK(!vm_file_cancel(f));
    unsigned before = attempts;
    fail_at = before + 1;
    CHECK(vm_demand_begin(f,0,1,&demand_profile,&out)<0 && errno==ECANCELED);
    CHECK(attempts==before && out==(void *)1);
    fail_at = 0;
    file_end(f);file_under_test=NULL;
}
static void demand_cleanup(void *p) {
    vm_file_t *f=file_under_test;
    if(!allocation(p)->backing) return;
    CHECK(f->info.loading_pages==1 && f->demand_uses==2 && f->demand_error);
    CHECK(f->staging && fd_table[f->fd] && demand_other->staging && fd_table[demand_other->fd]);
    CHECK(!demand_other->info.cancelled && !demand_other->demand_error);
    {irq_disable_scoped();CHECK(!vm_demand_terminal_locked(demand_users[0]));}
    ++demand_cleanup_checks;
    if(atomic_load(&demand_hold_cleanup)) {
        atomic_store(&demand_cleanup_entered,true);
        while(!atomic_load(&demand_cleanup_resume)) sched_yield();
    }
}
static void demand_failure(unsigned mode) {
    vm_file_t *f=demand_setup();demand_other=file_open_options(file_options());
    unsigned other_charge=demand_other->info.charged_bytes;
    demand_users[0]=demand_begin_test(f,0,0,1);demand_users[1]=demand_begin_test(f,1,0,1);
    demand_save(demand_users[0],0,0);demand_save(demand_users[1],1,0);
    size_t retained=f->info.charged_bytes;unsigned reads=source_reads,closes=source_closes;
    int expected=EIO;
    if(mode==0) {source_seek_error=ESPIPE;expected=ESPIPE;}
    if(mode==1) seek_delta=1;
    if(mode==2) read_delta=-4096;
    if(mode==3) read_delta=-1;
    if(mode==4 || mode==10) {source_read_error=ENODEV;expected=ENODEV;}
    if(mode==5 || mode==6 || mode==11) {fail_at=attempts+(mode==5?1:mode==6?2:3);expected=ENOMEM;}
    if(mode==8) {purge_inject=cancel_file;expected=ECANCELED;}
    free_inject=demand_cleanup;
    if(mode==7) {
        expected=ECANCELED;
        atomic_store(&demand_hold_cleanup,true);
        atomic_store(&read_park,true);atomic_store(&read_entered,false);atomic_store(&read_resume,false);
        unsigned waits=atomic_load(&demand_waits);
        demand_worker_t wa={demand_users[0],0,0,0},wb={demand_users[1],1,0,0};pthread_t ta,tb;
        CHECK(!pthread_create(&ta,NULL,demand_worker,&wa));
        while(!atomic_load(&read_entered)) sched_yield();
        CHECK(!pthread_create(&tb,NULL,demand_worker,&wb));
        while(atomic_load(&demand_waits)==waits) sched_yield();
        CHECK(!vm_file_cancel(f));demand_refusal(f,demand_users[0]);
        {irq_disable_scoped();CHECK(!vm_demand_terminal_locked(demand_users[0]) && !vm_demand_terminal_locked(demand_users[1]));}
        source_read_error=ENODEV;atomic_store(&read_resume,true);
        while(!atomic_load(&demand_cleanup_entered)) sched_yield();
        CHECK(f->info.loading_pages && f->staging && fd_table[f->fd] && f->demand_uses==2);
        {irq_disable_scoped();CHECK(!vm_demand_terminal_locked(demand_users[0]) && !vm_demand_terminal_locked(demand_users[1]));}
        atomic_store(&demand_cleanup_resume,true);
        CHECK(!pthread_join(ta,NULL) && !pthread_join(tb,NULL));
        CHECK(wa.rc<0 && wb.rc<0 && wa.error==expected && wb.error==expected);
    } else {
        demand_select(0);int rc=vm_demand_prepare(demand_users[0]);
        if(mode==9 || mode==12) {
            CHECK(!rc);
            if(mode==9) {CHECK(!vm_file_cancel(f));expected=ECANCELED;}
            else {mmucr_value=0;expected=EBUSY;}
            irq_context_t *saved=NULL,*dest=NULL;
            {irq_disable_scoped();CHECK(!vm_demand_retry_locked(demand_users[0],&saved,&dest) && !saved && !dest);}
            CHECK(vm_demand_prepare(demand_users[0])<0 && errno==expected);
        } else CHECK(rc<0 && errno==expected);
        if(mode==10) CHECK(!vm_file_cancel(f));
        demand_select(1);CHECK(vm_demand_prepare(demand_users[1])<0 && errno==expected);
    }
    CHECK(f->demand_error==expected && !f->info.loading_pages && f->demand_uses==2);
    CHECK(f->info.charged_bytes==retained+((mode==9 || mode==12)?4096:0));
    CHECK(f->region->pages[0].demand_refs==2 && !!f->region->pages[0].protection==(mode==9 || mode==12));
    CHECK(source_reads<=reads+1 && source_closes==closes && source_refs==2);
    CHECK(demand_other->info.charged_bytes==other_charge && !demand_other->info.cancelled);
    for(unsigned i=0;i<2;++i) {
        demand_refusal(f,demand_users[i]);
        CHECK(!memcmp(&demand_users[i]->saved,&demand_original[i],sizeof(irq_context_t)));
        irq_disable_scoped();CHECK(vm_demand_terminal_locked(demand_users[i]));
    }
    vm_demand_t *out=(void *)1;demand_select(2);
    if(mode==12) mmucr_value=1;
    unsigned before = attempts;
    fail_at = before + 1;
    CHECK(vm_demand_begin(f,0,1,&demand_profile,&out)<0 && errno==expected && out==(void *)1);
    CHECK(attempts==before);
    if(mode!=5 && mode!=9 && mode!=12) CHECK(demand_cleanup_checks);
    /* Deliberately retain failed owners. Only the host test process ends. */
}
static void demand_tests(void) {
    alarm(45);
    demand_admission();
    demand_reclaim();
    for(unsigned mode=0;mode<4;++mode) demand_success(mode);
    for(unsigned mode=0;mode<13;++mode) {
        pid_t pid=fork();CHECK(pid>=0);
        if(!pid) {alarm(10);demand_failure(mode);_exit(0);}
        int status;CHECK(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    alarm(0);
    puts("DP1 actual VM/VFS ownership, reclaim, sharing, serialization, failure/cancel/retention PASS");
}
