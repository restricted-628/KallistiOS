"""Compile VM and checked MMU production code with host platform substitutes."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', default='cc')
p.add_argument('--cflags', default='-std=gnu17 -O2 -Wall -Wextra -Werror')
p.add_argument('--vm-source', type=Path, help='Alternate source for negative controls')
a = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
mmu = (root / 'kernel/arch/dreamcast/kernel/mmu.c').read_text()
def section(start, end):
    assert mmu.count(start) == 1
    return start + mmu.split(start, 1)[1].split(end, 1)[0]
production = section('#define MMU_IND_BITS', '/********************************************************************************/')
production += section('#define BUILD_PTEH', '#define SET_TTB')
production += section('/* Table management */', '#if 0   /* Only applies to KOS-MMU */')
# Execute the actual VFS wrapper/descriptor paths, with only platform/backend
# substitutes. In particular fs_close is not a consumed-close stub.
fs = (root / 'kernel/fs/fs.c').read_text()
def fs_function(signature):
    assert fs.count(signature) == 1
    return signature + fs.split(signature, 1)[1].split('\n}', 1)[0] + '\n}\n'
vfs = fs[fs.index('typedef struct fs_hnd {'):fs.index('/* Internal file commands')]
for signature in [
    'static fs_hnd_t *fs_root_opendir(void) {',
    'static fs_hnd_t *fs_hnd_open(const char *fn, int mode) {',
    'static void fs_hnd_ref(fs_hnd_t *ref) {',
    'static int fs_hnd_unref(fs_hnd_t *ref) {',
    'static file_t fs_hnd_assign(fs_hnd_t *hnd) {',
    'file_t fs_open(const char *fn, int mode) {',
    'static fs_hnd_t *fs_map_hnd_ref(file_t fd) {',
    'static void fs_hnd_cleanup(fs_hnd_t **hnd) {',
    'int fs_close(file_t fd) {',
    'ssize_t fs_read(file_t fd, void *buffer, size_t cnt) {',
    'off_t fs_seek(file_t fd, off_t offset, int whence) {',
    'int64_t fs_total64(file_t fd) {',
    'int fs_fstat(file_t fd, struct stat *st) {',
]:
    vfs += fs_function(signature)
vm = (a.vm_source or root / 'kernel/mm/vm.c').read_text()
sr = '__asm__ volatile("stc sr, %0" : "=r"(sr));'
assert vm.count(sr) == 1
# Only the SH-4 register read is replaced. All VM control flow is verbatim.
vm = vm.replace(sr, 'sr = test_sr;')
with tempfile.TemporaryDirectory(prefix='kos-vm-test.') as td:
    tmp = Path(td)
    (tmp / 'kos').mkdir()
    (tmp / 'kos/vm.h').symlink_to(root / 'include/kos/vm.h')
    (tmp / 'kos/vm_exec.h').symlink_to(root / 'include/kos/vm_exec.h')
    (tmp / 'kos/fs.h').write_text('''#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <limits.h>
typedef int file_t;
typedef int64_t _off64_t;
#define FILEHND_INVALID (-1)
#define O_DIR 0x10000000
#define NMMGR_TYPE_VFS 1
#define DBG_ERROR 0
#define dbglog(...) ((void)0)
typedef struct { const char *pathname; unsigned type; } nmmgr_handler_t;
typedef struct vfs_handler vfs_handler_t;
struct vfs_handler {
    nmmgr_handler_t nmmgr;
    void *(*open)(vfs_handler_t *, const char *, int);
    int (*close)(void *);
    ssize_t (*read)(void *, void *, size_t);
    off_t (*seek)(void *, off_t, int);
    _off64_t (*seek64)(void *, _off64_t, int);
    size_t (*total)(void *);
    uint64_t (*total64)(void *);
    int (*fstat)(void *, struct stat *);
};
file_t fs_open(const char *, int);
int fs_close(file_t);
ssize_t fs_read(file_t, void *, size_t);
off_t fs_seek(file_t, off_t, int);
int64_t fs_total64(file_t);
int fs_fstat(file_t, struct stat *);
''')
    (tmp / 'kos/cdefs.h').write_text('#include <sys/cdefs.h>\n')
    (tmp / 'kos/irq.h').write_text('''#pragma once
typedef uint32_t irq_t;
typedef struct irq_context { uint32_t r[16], pc, sr; } irq_context_t;
#define EXC_DTLB_MISS_READ 0x40u
#define EXC_DTLB_MISS_WRITE 0x60u
static inline int irq_inside_int(void) { return test_irq; }
''')
    (tmp / 'vm-internal.h').symlink_to(root / 'kernel/mm/vm-internal.h')
    (tmp / 'kos/cache.h').write_text('#pragma once\nvoid dcache_purge_range(uintptr_t, size_t);\nvoid icache_inval_range(uintptr_t, size_t);\n')
    (tmp / 'kos/mutex.h').write_text('''#pragma once
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define MUTEX_TYPE_NORMAL 0
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static int mutex_init(mutex_t *m, int type) { (void)type; return pthread_mutex_init(m, NULL); }
static int mutex_lock(mutex_t *m) {
    if(fail_lock) { errno = EAGAIN; return -1; }
    if(m == watched_mutex) atomic_fetch_add(&mutex_waiters, 1);
    return pthread_mutex_lock(m);
}
static int mutex_unlock(mutex_t *m) { return pthread_mutex_unlock(m); }
static int mutex_destroy(mutex_t *m) { return pthread_mutex_destroy(m); }
''')
    (tmp / 'mmu.inc').write_text(production)
    (tmp / 'vm.inc').write_text(vm)
    (tmp / 'fs.inc').write_text(vfs)
    exe = tmp / 'test'
    command = shlex.split(a.cc) + shlex.split(a.cflags) + ['-pthread',
        '-I' + str(tmp),
        '-I' + str(root / 'kernel/arch/dreamcast/include'),
        str(here / 'test.c'), '-o', str(exe)]
    print('COMPILE:', shlex.join(command), flush=True)
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
