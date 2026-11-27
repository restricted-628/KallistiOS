/* Target-only: instrument ordinary I/O and genwait, NOT the exception entry.
   Link with --wrap=fs_read,--wrap=genwait_wait. */
#define main cold_example_main
#include "../../examples/dreamcast/basic/mmu/demand-disc/demand-disc.c"
#undef main

static struct {
    vm_file_t *file;
    vm_file_demand_t *use[2];
    unsigned page[2];
    volatile unsigned begun, entered, waiting, release, finished;
    unsigned reads;
    int result[2];
    kthread_t *threads[2];
} m;
static _Alignas(32) unsigned char stacks[2][65536];
ssize_t __real_fs_read(file_t, void *, size_t);
int __real_genwait_wait(void *, const char *, unsigned);

ssize_t __wrap_fs_read(file_t fd, void *buf, size_t size) {
    if(m.begun == 3 && (thd_current == m.threads[0] || thd_current == m.threads[1])) {
        ++m.reads;
        if(!m.entered) {
            m.entered = 1;
            while(!m.release)
                thd_pass();
#if DP1_FAULT_CONTROL == 1
            errno = EIO;
            return -1;
#endif
        }
    }
    return __real_fs_read(fd, buf, size);
}
int __wrap_genwait_wait(void *obj, const char *why, unsigned timeout) {
    if(why && !strcmp(why, "vm demand load"))
        m.waiting = 1;
    return __real_genwait_wait(obj, why, timeout);
}
static void *load_peer(void *arg) {
    uintptr_t i = (uintptr_t)arg;
    if(vm_file_demand_begin(m.file, m.page[i], 1, &m.use[i]) < 0)
        return NULL;
    { irq_disable_scoped(); m.begun |= 1u << i; }
    while(m.begun != 3 || (i && !m.entered))
        thd_pass();
    errno = EDOM;
    volatile const unsigned char *p = (void *)(arena + m.page[i] * PAGE_BYTES);
    unsigned char actual = *p;
    if(errno != EDOM || actual != source_byte(SOURCE_OFFSET + m.page[i] * PAGE_BYTES))
        return NULL;
    { irq_disable_scoped(); m.finished |= 1u << i; }
    while(m.finished != 3)
        thd_pass();
    m.result[i] = vm_file_demand_end(m.use[i]) == 0;
    return NULL;
}
static bool await(volatile unsigned *p, unsigned value) {
    uint64_t end = timer_ms_gettime64() + 3000;
    while(*p != value && timer_ms_gettime64() < end)
        thd_pass();
    return *p == value;
}
static bool paired(vm_file_t *file, bool different) {
    memset(&m, 0, sizeof(m));
    m.file = file; m.page[1] = different;
    memset(stacks, 0xa5, sizeof(stacks));
    for(uintptr_t i = 0; i < 2; ++i) {
        const kthread_attr_t attr = {.stack_ptr = stacks[i], .stack_size = sizeof(stacks[i])};
        m.threads[i] = thd_create_ex(&attr, load_peer, (void *)i);
        if(!m.threads[i]) return false;
    }
    if(!await(&m.waiting, 1)) return false;
    /* No PTE yet: load is stopped in ordinary context and peer is joining it. */
    vm_file_page_info_t info;
    if(vm_file_page_query(file, 0, &info) < 0 || info.state != VM_FILE_LOADING ||
       vm_file_demand_end(m.use[0]) != -1 || errno != EPERM ||
       vm_file_evict(file, 0) != -1 || errno != EBUSY) return false;
    /* Read the same optical file independently while the retained fd is busy. */
    file_t other = fs_open("/cd/VM4DATA.BIN", O_RDONLY);
    unsigned char value;
    if(other < 0 || fs_read(other, &value, 1) != 1 || value != source_byte(0) ||
       fs_close(other) < 0) return false;
#if DP1_FAULT_CONTROL == 2
    if(vm_file_cancel(file) < 0) return false;
#endif
#if DP1_FAULT_CONTROL
    dbgio_write_str("DP1PAIR ARMED failure cleanup and halt\n");
#endif
    m.release = 1;
    if(!await(&m.finished, 3)) return false;
    for(unsigned i = 0; i < 2; ++i) {
        if(thd_join(m.threads[i], NULL) < 0 || !m.result[i]) return false;
        size_t n = 0;
        while(n < sizeof(stacks[i]) && stacks[i][n] == 0xa5) ++n;
        if(n < 8192) return false;
        dbgio_printf("DP1PAIR side=%u touched=%u\n", i, (unsigned)(sizeof(stacks[i]) - n));
    }
    dbgio_printf("DP1PAIR different=%d reads=%u wait=%u\n", different, m.reads, m.waiting);
    return m.reads == 1u + different && !vm_file_evict(file, 0) &&
        (!different || !vm_file_evict(file, 1));
}
int main(void) {
    if(dbgio_dev_select("scif") < 0 || dbgio_set_irq_usage(0) < 0) return 1;
    mmucontext_t *prev = mmu_cxt_current, *c = mmu_context_create(77);
    if(!c) return 1;
    select_context(c);
    vm_space_t *s = vm_space_create(c, (void *)arena, 3 * PAGE_BYTES);
    vm_file_options_t opt = {.requested=(void *)arena, .offset=SOURCE_OFFSET,
        .bytes=SOURCE_BYTES, .resident_limit=2, .budget_bytes=40000};
    vm_file_t *f;
    if(!s || check_translation(c) < 0 ||
       fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) < 0 ||
       vm_file_open(s, "/cd/VM4DATA.BIN", &opt, &f) < 0) goto failed;
    if(!paired(f, false) || !paired(f, true)) goto failed;
    int clean = 0;
    if(vm_file_close(f, &clean) || clean || vm_space_destroy(s)) goto failed;
    select_context(prev); mmu_context_destroy(c);
    dbgio_write_str("DP1PAIR PASS shared=1 different=1 foreign=2 optical=2\n");
    return 0;
failed:
    dbgio_write_str("DP1PAIR FAIL\n");
    arch_panic("direct ISO pair fixture");
}
